
#ifndef AUCTION_MANAGER_H
#define AUCTION_MANAGER_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>
#include <string>
#include <ctime>
#include <cstdio>
#include <iostream>

#include "bst.h"
#include "linked_list.h"
#include "queue.h"
#include "models.h"
#include "json_util.h"

//  Notification — one entry in the FIFO event feed
struct Notification {
    std::string message;
    time_t      timestamp;
    int         relatedAuctionId;

    JsonObject toJson() const {
        JsonObject obj;
        obj.set("message", message)
           .set("timestamp", (long long)timestamp)
           .set("auctionId", relatedAuctionId);
        return obj;
    }
};


// AuctionManager — Central Auction Engine                                

class AuctionManager {
private:
    // ── Storage ───────────────────────────────────────────────────────────
    std::vector<User*>    users;       // indexed by (id - 1)  [array/vector]
    std::vector<Auction*> auctions;    // indexed by (id - 1)  [array/vector]
    MinBST                expiryTree;  // global Min-BST for close-time tracking
    Queue<Notification>   notifications; // FIFO queue (capped at 200)

    // ── ID generators ─────────────────────────────────────────────────────
    int nextUserId;
    int nextAuctionId;
    int nextBidId;

    // ── Thread safety ─────────────────────────────────────────────────────
    CRITICAL_SECTION cs;               // reentrant mutex (Windows)

public:
    AuctionManager()
        : notifications(200), nextUserId(1), nextAuctionId(1), nextBidId(1)
    {
        InitializeCriticalSection(&cs);
    }

    ~AuctionManager() {
        for (size_t i = 0; i < users.size(); i++)    delete users[i];
        for (size_t i = 0; i < auctions.size(); i++) delete auctions[i];
        DeleteCriticalSection(&cs);
    }

    void lock()   { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }

    //  USER OPERATIONS
    
    User* createUser(const std::string& name, const std::string& email) {
        lock();
        User* u  = new User();
        u->id    = nextUserId++;
        u->name  = name;
        u->email = email;
        users.push_back(u);

        Notification n;
        n.message          = "New user registered: " + name;
        n.timestamp        = time(NULL);
        n.relatedAuctionId = 0;
        notifications.enqueue(n);

        unlock();
        return u;
    }

    User* getUser(int id) {
        if (id < 1 || id > (int)users.size()) return NULL;
        return users[id - 1];
    }

    std::vector<User*> getAllUsers() {
        return users;
    }

    //  AUCTION OPERATIONS

    Auction* createAuction(const std::string& title, const std::string& desc,
                           const std::string& imageUrl, double startingPrice,
                           int durationSeconds)
    {
        lock();
        Auction* a      = new Auction();
        a->id           = nextAuctionId++;
        a->title        = title;
        a->description  = desc;
        a->imageUrl     = imageUrl;
        a->startingPrice= startingPrice;
        a->startTime    = time(NULL);
        a->endTime      = a->startTime + durationSeconds;
        a->isActive     = true;

        auctions.push_back(a);

        expiryTree.insert(a->endTime, a->id);

        Notification n;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "New auction created: \"%s\" (Starting at $%.2f, Duration: %ds)",
            title.c_str(), startingPrice, durationSeconds);
        n.message          = std::string(buf);
        n.timestamp        = time(NULL);
        n.relatedAuctionId = a->id;
        notifications.enqueue(n);

        std::cout << "[AUCTION] Created: " << title
                  << " | Ends at: " << a->endTime << std::endl;
        unlock();
        return a;
    }

    Auction* getAuction(int id) {
        if (id < 1 || id > (int)auctions.size()) return NULL;
        return auctions[id - 1];
    }

    std::vector<Auction*> getActiveAuctions() {
        std::vector<Auction*> result;
        for (size_t i = 0; i < auctions.size(); i++) {
            if (auctions[i]->isActive) result.push_back(auctions[i]);
        }
        return result;
    }

    std::vector<Auction*> getClosedAuctions() {
        std::vector<Auction*> result;
        for (size_t i = 0; i < auctions.size(); i++) {
            if (!auctions[i]->isActive) result.push_back(auctions[i]);
        }
        return result;
    }

    std::vector<Auction*> getAllAuctions() {
        return auctions;
    }

    //  BID PLACEMENT

    // Returns empty string on success, or an error message on failure.
    std::string placeBid(int auctionId, int userId, double amount) {
        lock();

        Auction* a = getAuction(auctionId);
        if (!a) { unlock(); return "Auction not found"; }
        if (!a->isActive) { unlock(); return "Auction is already closed"; }
        if (time(NULL) >= a->endTime) { unlock(); return "Auction has expired"; }

        User* u = getUser(userId);
        if (!u) { unlock(); return "User not found"; }

        // Validate: bid must exceed current highest (or starting price)
        double minBid = (a->highestBid > 0) ? a->highestBid : a->startingPrice;
        if (amount <= minBid) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                "Bid must be higher than $%.2f", minBid);
            unlock();
            return std::string(buf);
        }

        // ── Insert into the per-auction Max-BST ──────────────────────────
        int bidId = nextBidId++;
        a->bidTree.insert(amount, bidId, userId, u->name, time(NULL));
        a->highestBid = amount;
        a->totalBids++;

        // ── Append to user's LinkedList bid history ──────────────────────
        BidRecord record;
        record.auctionId    = auctionId;
        record.auctionTitle = a->title;
        record.amount       = amount;
        record.timestamp    = time(NULL);
        record.isWinning    = false;
        u->bidHistory.append(record);

        // ── Notification ─────────────────────────────────────────────────
        Notification n;
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "%s placed a bid of $%.2f on \"%s\"",
            u->name.c_str(), amount, a->title.c_str());
        n.message          = std::string(buf);
        n.timestamp        = time(NULL);
        n.relatedAuctionId = auctionId;
        notifications.enqueue(n);

        std::cout << "[BID] " << u->name << " → $" << amount
                  << " on \"" << a->title << "\"" << std::endl;

        unlock();
        return "";   // success
    }

    // =====================================================================
    //  AUTOMATED EXPIRY — Called by the background thread every second
    // =====================================================================

    void checkAndCloseExpired() {
        lock();

        time_t now = time(NULL);

        // Keep draining the Min-BST as long as the leftmost node has expired
        while (true) {
            time_t closeTime;
            int    auctionId;
            if (!expiryTree.peekMin(closeTime, auctionId)) break;
            if (closeTime > now) break;   // nothing expired yet

            // ── Remove from Min-BST ──────────────────────────────────────
            expiryTree.removeMin();

            Auction* a = getAuction(auctionId);
            if (!a || !a->isActive) continue;

            // ── Close the auction ────────────────────────────────────────
            a->isActive = false;
            std::cout << "\n[EXPIRY] Closing auction: " << a->title << std::endl;

            if (a->bidTree.isEmpty()) {
                // No bids were placed
                Notification n;
                n.message = "Auction \"" + a->title + "\" closed with no bids.";
                n.timestamp        = now;
                n.relatedAuctionId = auctionId;
                notifications.enqueue(n);
                std::cout << "  → No bids. Auction ended without a winner.\n";
                continue;
            }

            // ── Vickrey (Second-Price) Settlement ────────────────────────
            //  1. Rightmost node of Max-BST  → highest bidder (WINNER)
            //  2. In-order predecessor of max → second-highest (PRICE)
            BidBSTNode* maxNode = a->bidTree.findMax();
            a->winnerId   = maxNode->data.userId;
            a->winnerName = maxNode->data.userName;
            a->highestBid = maxNode->data.amount;

            BidBSTNode* secondMax = a->bidTree.findSecondMax();
            if (secondMax && secondMax != maxNode) {
                a->settledPrice = secondMax->data.amount;   // Vickrey price
            } else {
                a->settledPrice = maxNode->data.amount;     // only one bidder
            }

            // ── Update winner's bid history ──────────────────────────────
            User* winner = getUser(a->winnerId);
            if (winner) {
                for (int i = 0; i < winner->bidHistory.size(); i++) {
                    BidRecord& rec = winner->bidHistory.get(i);
                    if (rec.auctionId == auctionId) {
                        rec.isWinning = true;
                    }
                }
            }

            // ── Notification ─────────────────────────────────────────────
            Notification n;
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "AUCTION CLOSED: \"%s\" | Winner: %s | "
                "Highest Bid: $%.2f | Settled Price (Vickrey): $%.2f | "
                "Total Bids: %d",
                a->title.c_str(), a->winnerName.c_str(),
                a->highestBid, a->settledPrice, a->totalBids);
            n.message          = std::string(buf);
            n.timestamp        = now;
            n.relatedAuctionId = auctionId;
            notifications.enqueue(n);

            std::cout << "  → Winner: " << a->winnerName
                      << " | Highest Bid: $" << a->highestBid
                      << " | Vickrey Price: $" << a->settledPrice
                      << " | Bids: " << a->totalBids << std::endl;
        }

        unlock();
    }

    // =====================================================================
    //  NOTIFICATIONS
    // =====================================================================

    std::vector<Notification> getNotifications() {
        lock();
        std::vector<Notification> result = notifications.toVector();
        unlock();
        return result;
    }

    // =====================================================================
    //  SEED DEMO DATA — pre-populates users, auctions, and bids for demo
    // =====================================================================

    void seedDemoData() {
        std::cout << "[SEED] Creating demo users...\n";
        createUser("Alice Johnson",  "alice@example.com");
        createUser("Bob Smith",      "bob@example.com");
        createUser("Charlie Brown",  "charlie@example.com");
        createUser("Diana Prince",   "diana@example.com");
        createUser("Ethan Hunt",     "ethan@example.com");

        std::cout << "[SEED] Creating demo auctions...\n";

        createAuction(
            "Vintage Mechanical Watch",
            "A rare 1960s Swiss mechanical watch in pristine condition with exhibition caseback and leather strap.",
            "https://images.unsplash.com/photo-1524592094714-0f0654e20314?w=400&h=300&fit=crop",
            150.00, 120);   // 2 min

        createAuction(
            "Gaming Laptop RTX 4080",
            "High-performance gaming laptop: RTX 4080, Intel i9-13900H, 32GB DDR5, 1TB NVMe SSD, 240Hz QHD display.",
            "https://images.unsplash.com/photo-1603302576837-37561b2e2302?w=400&h=300&fit=crop",
            800.00, 180);   // 3 min

        createAuction(
            "Original Oil Painting - Sunset",
            "Stunning landscape oil painting on canvas, 24x36 inches, signed by emerging contemporary artist.",
            "https://images.unsplash.com/photo-1579783902614-a3fb3927b6a5?w=400&h=300&fit=crop",
            200.00, 240);   // 4 min

        createAuction(
            "Antique Wooden Chess Set",
            "Hand-carved mahogany chess set from the 1920s. Complete 32 pieces with original inlaid board.",
            "https://images.unsplash.com/photo-1586165368502-1bad9cc4be10?w=400&h=300&fit=crop",
            75.00, 300);    // 5 min

        createAuction(
            "Signed First-Edition Novel",
            "First-edition hardcover signed by the author on publication day. Dust jacket in excellent condition.",
            "https://images.unsplash.com/photo-1544947950-fa07a98d237f?w=400&h=300&fit=crop",
            50.00, 600);    // 10 min

        createAuction(
            "Professional DSLR Camera Kit",
            "Full-frame DSLR body with 3 prime lenses (35mm, 50mm, 85mm), speedlite flash, and hard-shell case.",
            "https://images.unsplash.com/photo-1516035069371-29a1b244cc32?w=400&h=300&fit=crop",
            500.00, 420);   // 7 min

        std::cout << "[SEED] Placing initial demo bids...\n";

        placeBid(1, 2, 175.00);   // Bob    → Watch
        placeBid(1, 3, 210.00);   // Charlie→ Watch
        placeBid(1, 4, 230.00);   // Diana  → Watch

        placeBid(2, 1, 850.00);   // Alice  → Laptop
        placeBid(2, 4, 920.00);   // Diana  → Laptop
        placeBid(2, 5, 950.00);   // Ethan  → Laptop

        placeBid(3, 1, 250.00);   // Alice  → Painting
        placeBid(3, 3, 280.00);   // Charlie→ Painting

        placeBid(4, 2, 100.00);   // Bob    → Chess Set
        placeBid(4, 5, 125.00);   // Ethan  → Chess Set

        placeBid(5, 3,  65.00);   // Charlie→ Book

        placeBid(6, 4, 550.00);   // Diana  → Camera
        placeBid(6, 1, 600.00);   // Alice  → Camera

        std::cout << "[SEED] Demo data ready! "
                  << users.size()    << " users, "
                  << auctions.size() << " auctions, "
                  << (nextBidId - 1) << " bids.\n\n";
    }
};

#endif // AUCTION_MANAGER_H
