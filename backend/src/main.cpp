#include "../include/http_server.h"
#include "../include/json_util.h"
#include "../include/auction_manager.h"

#include <iostream>
#include <cstdlib>
#include <string>

// Global auction manager accessed by HTTP handlers and expiry thread
AuctionManager g_manager;

// Background thread to automatically close expired auctions every second
DWORD WINAPI expiryThreadFunc(LPVOID param) {
    (void)param;
    std::cout << "[THREAD] Background expiry checker running...\n";
    while (true) {
        Sleep(1000);          // 1-second interval
        g_manager.checkAndCloseExpired();
    }
    return 0;
}

int main() {
    std::cout << "Starting Real-Time Auction System...\n";

    // Step 1: Seed demo data
    std::cout << "Seeding Demo Data...\n";
    g_manager.seedDemoData();

    // Step 2: Start background expiry thread
    HANDLE hThread = CreateThread(NULL, 0, expiryThreadFunc, NULL, 0, NULL);
    if (!hThread) {
        std::cerr << "[ERROR] Failed to create background thread!\n";
        return 1;
    }

    // Step 3: Configure HTTP server and static files directory
    HttpServer server;
    server.setStaticDir("frontend");

    // ────────────────────────────────────────────────────────────────────────
    // REST API ROUTES
    // ────────────────────────────────────────────────────────────────────────

    // POST /api/users — Create a new user
    server.post("/api/users", [](const HttpRequest& req, HttpResponse& res) {
        std::map<std::string, std::string> body = parseJson(req.body);
        std::string name  = body["name"];
        std::string email = body["email"];

        if (name.empty()) {
            res.error(400, "Name is required");
            return;
        }

        User* u = g_manager.createUser(name, email);
        res.json(JsonObject()
            .set("success", true)
            .setRaw("user", u->toJson().toString())
            .toString());
    });

    // GET /api/users — List all users
    server.get("/api/users", [](const HttpRequest& req, HttpResponse& res) {
        (void)req;
        g_manager.lock();
        std::vector<User*> users = g_manager.getAllUsers();
        JsonArray arr;
        for (size_t i = 0; i < users.size(); i++) {
            arr.add(users[i]->toJson());
        }
        g_manager.unlock();
        res.json(JsonObject().setRaw("users", arr.toString()).toString());
    });

    // GET /api/users/:id — Get user details and bid history
    server.get("/api/users/:id", [](const HttpRequest& req, HttpResponse& res) {
        int id = std::atoi(req.params.at("id").c_str());

        g_manager.lock();
        User* u = g_manager.getUser(id);
        if (!u) {
            g_manager.unlock();
            res.error(404, "User not found");
            return;
        }

        JsonObject obj = u->toJson();

        // Serialize LinkedList bid history
        JsonArray bids;
        for (int i = 0; i < u->bidHistory.size(); i++) {
            const BidRecord& rec = u->bidHistory.get(i);
            bids.add(JsonObject()
                .set("auctionId", rec.auctionId)
                .set("auctionTitle", rec.auctionTitle)
                .set("amount", rec.amount)
                .set("timestamp", (long long)rec.timestamp)
                .set("isWinning", rec.isWinning));
        }
        obj.setRaw("bidHistory", bids.toString());

        g_manager.unlock();
        res.json(obj.toString());
    });

    // GET /api/auctions — List active auctions
    server.get("/api/auctions", [](const HttpRequest& req, HttpResponse& res) {
        (void)req;
        g_manager.lock();
        std::vector<Auction*> active = g_manager.getActiveAuctions();
        JsonArray arr;
        for (size_t i = 0; i < active.size(); i++) {
            arr.add(active[i]->toJson());
        }
        g_manager.unlock();
        res.json(JsonObject().setRaw("auctions", arr.toString()).toString());
    });

    // GET /api/auctions/:id — Get single auction with its bids
    server.get("/api/auctions/:id", [](const HttpRequest& req, HttpResponse& res) {
        int id = std::atoi(req.params.at("id").c_str());

        g_manager.lock();
        Auction* a = g_manager.getAuction(id);
        if (!a) {
            g_manager.unlock();
            res.error(404, "Auction not found");
            return;
        }

        JsonObject obj = a->toJson();

        // Serialize BST bids sorted by amount
        std::vector<BidInfo> bids = a->bidTree.getAllBidsSorted();
        JsonArray bidArr;
        for (size_t i = 0; i < bids.size(); i++) {
            bidArr.add(JsonObject()
                .set("bidId", bids[i].bidId)
                .set("userId", bids[i].userId)
                .set("userName", bids[i].userName)
                .set("amount", bids[i].amount)
                .set("timestamp", (long long)bids[i].timestamp));
        }
        obj.setRaw("bids", bidArr.toString());

        g_manager.unlock();
        res.json(obj.toString());
    });

    // POST /api/auctions — Create a new auction
    server.post("/api/auctions", [](const HttpRequest& req, HttpResponse& res) {
        std::map<std::string, std::string> body = parseJson(req.body);
        std::string title    = body["title"];
        std::string desc     = body["description"];
        std::string imageUrl = body["imageUrl"];

        double startingPrice = 0;
        int    duration      = 300;   // Default 5 minutes

        if (body.count("startingPrice") && !body["startingPrice"].empty())
            startingPrice = std::atof(body["startingPrice"].c_str());
        if (body.count("durationSeconds") && !body["durationSeconds"].empty())
            duration = std::atoi(body["durationSeconds"].c_str());

        if (title.empty()) {
            res.error(400, "Title is required");
            return;
        }

        Auction* a = g_manager.createAuction(title, desc, imageUrl, startingPrice, duration);
        res.json(JsonObject()
            .set("success", true)
            .setRaw("auction", a->toJson().toString())
            .toString());
    });

    // POST /api/auctions/:id/bid — Place a bid
    server.post("/api/auctions/:id/bid", [](const HttpRequest& req, HttpResponse& res) {
        int auctionId = std::atoi(req.params.at("id").c_str());

        std::map<std::string, std::string> body = parseJson(req.body);
        int    userId = 0;
        double amount = 0;

        if (body.count("userId") && !body["userId"].empty())
            userId = std::atoi(body["userId"].c_str());
        if (body.count("amount") && !body["amount"].empty())
            amount = std::atof(body["amount"].c_str());

        if (userId <= 0 || amount <= 0) {
            res.error(400, "Valid userId and amount are required");
            return;
        }

        std::string error = g_manager.placeBid(auctionId, userId, amount);
        if (!error.empty()) {
            res.error(400, error);
            return;
        }

        g_manager.lock();
        Auction* a = g_manager.getAuction(auctionId);
        double highest = a ? a->highestBid : amount;
        g_manager.unlock();

        res.json(JsonObject()
            .set("success", true)
            .set("message", "Bid placed successfully!")
            .set("currentHighest", highest)
            .toString());
    });

    // GET /api/closed — List closed/settled auctions
    server.get("/api/closed", [](const HttpRequest& req, HttpResponse& res) {
        (void)req;
        g_manager.lock();
        std::vector<Auction*> closed = g_manager.getClosedAuctions();
        JsonArray arr;
        for (size_t i = 0; i < closed.size(); i++) {
            arr.add(closed[i]->toJson());
        }
        g_manager.unlock();
        res.json(JsonObject().setRaw("auctions", arr.toString()).toString());
    });

    // GET /api/notifications — Get notification feed
    server.get("/api/notifications", [](const HttpRequest& req, HttpResponse& res) {
        (void)req;
        std::vector<Notification> notifs = g_manager.getNotifications();
        JsonArray arr;
        for (int i = (int)notifs.size() - 1; i >= 0; i--) {
            arr.add(notifs[i].toJson());
        }
        res.json(JsonObject().setRaw("notifications", arr.toString()).toString());
    });

    // Step 4: Start HTTP server
    std::cout << "HTTP server running on port 8080.\n"
              << "Access http://localhost:8080 in your browser.\n\n";

    if (!server.listen(8080)) {
        std::cerr << "[ERROR] Server failed to start.\n";
        return 1;
    }

    CloseHandle(hThread);
    return 0;
}