// ============================================================================
//  http_server.h — Minimal HTTP Server for BidMatrix (Winsock-based)
// ============================================================================
//  A self-contained, zero-dependency HTTP/1.1 server for Windows providing:
//    • GET/POST route registration with path-parameter support (:id)
//    • Static file serving (for the frontend SPA)
//    • CORS headers for development convenience
//    • JSON response helpers
//
//  Uses Winsock2 directly — no external libraries required.
// ============================================================================

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <sstream>
#include <fstream>
#include <iostream>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
//  HttpRequest — parsed incoming request
// ─────────────────────────────────────────────────────────────────────────────
struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> params;    // path parameters  :id → "5"
    std::map<std::string, std::string> headers;
};

// ─────────────────────────────────────────────────────────────────────────────
//  HttpResponse — response to send back
// ─────────────────────────────────────────────────────────────────────────────
struct HttpResponse {
    int         status;
    std::string contentType;
    std::string body;

    HttpResponse() : status(200), contentType("application/json") {}

    void json(const std::string& jsonStr) {
        contentType = "application/json";
        body   = jsonStr;
        status = 200;
    }

    void error(int code, const std::string& message) {
        status      = code;
        contentType = "application/json";
        body        = "{\"error\":\"" + message + "\"}";
    }
};

// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║  HttpServer — Winsock-based HTTP/1.1 Server                             ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
class HttpServer {
public:
    typedef std::function<void(const HttpRequest&, HttpResponse&)> Handler;

private:
    struct Route {
        std::string method;
        std::vector<std::string> segments;   // split path pattern
        Handler handler;
    };

    std::vector<Route> routes;
    std::string        staticDir;
    SOCKET             serverSocket;
    bool               running;

    // ── Split a URL path into segments ────────────────────────────────────
    static std::vector<std::string> splitPath(const std::string& path) {
        std::vector<std::string> segs;
        std::string seg;
        for (size_t i = 0; i < path.size(); i++) {
            if (path[i] == '/') {
                if (!seg.empty()) { segs.push_back(seg); seg.clear(); }
            } else {
                seg += path[i];
            }
        }
        if (!seg.empty()) segs.push_back(seg);
        return segs;
    }

    // ── Match a route pattern against a request path ──────────────────────
    static bool matchRoute(const std::vector<std::string>& pattern,
                           const std::vector<std::string>& path,
                           std::map<std::string, std::string>& params)
    {
        if (pattern.size() != path.size()) return false;
        params.clear();
        for (size_t i = 0; i < pattern.size(); i++) {
            if (!pattern[i].empty() && pattern[i][0] == ':') {
                params[pattern[i].substr(1)] = path[i];
            } else if (pattern[i] != path[i]) {
                return false;
            }
        }
        return true;
    }

    // ── MIME type lookup ──────────────────────────────────────────────────
    static std::string getMimeType(const std::string& filePath) {
        size_t dot = filePath.rfind('.');
        if (dot == std::string::npos) return "application/octet-stream";
        std::string ext = filePath.substr(dot);
        if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
        if (ext == ".css")  return "text/css; charset=utf-8";
        if (ext == ".js")   return "application/javascript; charset=utf-8";
        if (ext == ".json") return "application/json";
        if (ext == ".png")  return "image/png";
        if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
        if (ext == ".gif")  return "image/gif";
        if (ext == ".svg")  return "image/svg+xml";
        if (ext == ".ico")  return "image/x-icon";
        return "application/octet-stream";
    }

    // ── Read a file from disk ─────────────────────────────────────────────
    static bool readFileContents(const std::string& path, std::string& out) {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file.is_open()) return false;
        std::ostringstream ss;
        ss << file.rdbuf();
        out = ss.str();
        return true;
    }

    // ── Parse raw HTTP request bytes ──────────────────────────────────────
    static HttpRequest parseRequest(const std::string& raw) {
        HttpRequest req;
        std::istringstream stream(raw);
        std::string line;

        // Request line: METHOD PATH HTTP/1.1
        if (std::getline(stream, line)) {
            if (!line.empty() && line[line.size()-1] == '\r')
                line.erase(line.size()-1);

            size_t sp1 = line.find(' ');
            size_t sp2 = line.find(' ', sp1 + 1);
            if (sp1 != std::string::npos && sp2 != std::string::npos) {
                req.method = line.substr(0, sp1);
                req.path   = line.substr(sp1 + 1, sp2 - sp1 - 1);
            }
        }

        // Headers
        while (std::getline(stream, line)) {
            if (!line.empty() && line[line.size()-1] == '\r')
                line.erase(line.size()-1);
            if (line.empty()) break;

            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                size_t start = val.find_first_not_of(' ');
                if (start != std::string::npos) val = val.substr(start);
                req.headers[key] = val;
            }
        }

        // Body
        std::ostringstream bodyStream;
        bodyStream << stream.rdbuf();
        req.body = bodyStream.str();

        // Strip query string
        size_t qmark = req.path.find('?');
        if (qmark != std::string::npos)
            req.path = req.path.substr(0, qmark);

        return req;
    }

    // ── Build HTTP response bytes ─────────────────────────────────────────
    static std::string buildResponse(const HttpResponse& res) {
        std::string statusText;
        switch (res.status) {
            case 200: statusText = "OK"; break;
            case 201: statusText = "Created"; break;
            case 204: statusText = "No Content"; break;
            case 400: statusText = "Bad Request"; break;
            case 404: statusText = "Not Found"; break;
            case 500: statusText = "Internal Server Error"; break;
            default:  statusText = "OK"; break;
        }

        std::ostringstream ss;
        ss << "HTTP/1.1 " << res.status << " " << statusText << "\r\n"
           << "Content-Type: "   << res.contentType  << "\r\n"
           << "Content-Length: " << res.body.size()  << "\r\n"
           << "Access-Control-Allow-Origin: *\r\n"
           << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type\r\n"
           << "Connection: close\r\n"
           << "\r\n"
           << res.body;
        return ss.str();
    }

public:
    HttpServer() : serverSocket(INVALID_SOCKET), running(false) {}

    // ── Register a GET route ──────────────────────────────────────────────
    void get(const std::string& path, Handler handler) {
        Route r;
        r.method   = "GET";
        r.segments = splitPath(path);
        r.handler  = handler;
        routes.push_back(r);
    }

    // ── Register a POST route ─────────────────────────────────────────────
    void post(const std::string& path, Handler handler) {
        Route r;
        r.method   = "POST";
        r.segments = splitPath(path);
        r.handler  = handler;
        routes.push_back(r);
    }

    // ── Set the directory for serving static files (frontend) ─────────────
    void setStaticDir(const std::string& dir) {
        staticDir = dir;
    }

    // ── Start listening on the given port (blocking) ──────────────────────
    bool listen(int port) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "[ERROR] WSAStartup failed.\n";
            return false;
        }

        serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (serverSocket == INVALID_SOCKET) {
            std::cerr << "[ERROR] Socket creation failed.\n";
            WSACleanup();
            return false;
        }

        // Allow address reuse
        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
                   (const char*)&opt, sizeof(opt));

        struct sockaddr_in addr;
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons((u_short)port);

        if (bind(serverSocket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "[ERROR] Bind failed on port " << port << ".\n";
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        if (::listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "[ERROR] Listen failed.\n";
            closesocket(serverSocket);
            WSACleanup();
            return false;
        }

        running = true;
        std::cout << "\n============================================\n"
                  << "  BidMatrix Server Running\n"
                  << "  http://localhost:" << port << "\n"
                  << "============================================\n\n";

        // ── Accept loop ──────────────────────────────────────────────────
        while (running) {
            SOCKET clientSocket = accept(serverSocket, NULL, NULL);
            if (clientSocket == INVALID_SOCKET) continue;

            // Read request data
            char buffer[8192];
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
            if (bytesRead <= 0) {
                closesocket(clientSocket);
                continue;
            }
            buffer[bytesRead] = '\0';

            std::string raw(buffer, bytesRead);
            HttpRequest req = parseRequest(raw);

            // Read remaining body if Content-Length indicates more data
            if (req.headers.count("Content-Length")) {
                int contentLength = std::atoi(req.headers["Content-Length"].c_str());

                // Find where body starts in raw data
                size_t headerEnd = raw.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    size_t bodyStart   = headerEnd + 4;
                    int    bodyHave    = (int)raw.size() - (int)bodyStart;
                    int    bodyNeed    = contentLength - bodyHave;

                    // Read remaining body bytes
                    while (bodyNeed > 0) {
                        int n = recv(clientSocket, buffer,
                                     std::min((int)sizeof(buffer)-1, bodyNeed), 0);
                        if (n <= 0) break;
                        buffer[n] = '\0';
                        raw.append(buffer, n);
                        bodyNeed -= n;
                    }

                    req.body = raw.substr(bodyStart, contentLength);
                }
            }

            HttpResponse res;

            // Handle CORS preflight
            if (req.method == "OPTIONS") {
                res.status = 204;
                res.body   = "";
                std::string r = buildResponse(res);
                send(clientSocket, r.c_str(), (int)r.size(), 0);
                closesocket(clientSocket);
                continue;
            }

            // ── Route matching ───────────────────────────────────────────
            bool matched = false;
            std::vector<std::string> pathSegs = splitPath(req.path);

            for (size_t i = 0; i < routes.size(); i++) {
                if (routes[i].method == req.method) {
                    std::map<std::string, std::string> params;
                    if (matchRoute(routes[i].segments, pathSegs, params)) {
                        req.params = params;
                        try {
                            routes[i].handler(req, res);
                        } catch (const std::exception& e) {
                            res.error(500, std::string("Internal error: ") + e.what());
                        }
                        matched = true;
                        break;
                    }
                }
            }

            // ── Static file serving ──────────────────────────────────────
            if (!matched && req.method == "GET" && !staticDir.empty()) {
                std::string filePath = staticDir;
                if (req.path == "/" || req.path.empty())
                    filePath += "/index.html";
                else
                    filePath += req.path;

                // Normalise separators for Windows
                for (size_t j = 0; j < filePath.size(); j++) {
                    if (filePath[j] == '/') filePath[j] = '\\';
                }

                std::string content;
                if (readFileContents(filePath, content)) {
                    res.status      = 200;
                    res.contentType = getMimeType(filePath);
                    res.body        = content;
                    matched         = true;
                }
            }

            if (!matched) {
                res.error(404, "Not Found");
            }

            // ── Send response ────────────────────────────────────────────
            std::string responseStr = buildResponse(res);
            send(clientSocket, responseStr.c_str(), (int)responseStr.size(), 0);
            closesocket(clientSocket);
        }

        closesocket(serverSocket);
        WSACleanup();
        return true;
    }

    void stop() {
        running = false;
        closesocket(serverSocket);
    }
};

#endif // HTTP_SERVER_H
