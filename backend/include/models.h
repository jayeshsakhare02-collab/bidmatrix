// ============================================================================
//  models.h — Domain Models for BidMatrix
// ============================================================================
//  Defines the core OOP classes:
//    • BidRecord  — lightweight record stored in per-user LinkedList history
//    • User       — user profile with linked-list bid history
//    • Auction    — auction item with Max-BST bid tree
// ============================================================================

#ifndef MODELS_H
#define MODELS_H

#include <string>
#include <ctime>
#include "bst.h"
#include "linked_list.h"
#include "json_util.h"

// ─────────────────────────────────────────────────────────────────────────────
//  BidRecord — one entry in a user's bidding history (stored in LinkedList)
// ─────────────────────────────────────────────────────────────────────────────
struct BidRecord {
    int         auctionId;
    std::string auctionTitle;
    double      amount;
    time_t      timestamp;
    bool        isWinning;    // updated when the auction closes

    BidRecord() : auctionId(0), amount(0), timestamp(0), isWinning(false) {}
};

// ─────────────────────────────────────────────────────────────────────────────
//  User — registered user with bid history tracked via LinkedList
// ─────────────────────────────────────────────────────────────────────────────
struct User {
    int                     id;
    std::string             name;
    std::string             email;
    LinkedList<BidRecord>   bidHistory;   // custom linked list — NOT std::list

    User() : id(0) {}

    JsonObject toJson() const {
        JsonObject obj;
        obj.set("id", id)
           .set("name", name)
           .set("email", email)
           .set("totalBids", bidHistory.size());
        return obj;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Auction — auction item with a Max-BST for bid management
// ─────────────────────────────────────────────────────────────────────────────
struct Auction {
    int         id;
    std::string title;
    std::string description;
    std::string imageUrl;
    double      startingPrice;
    time_t      startTime;
    time_t      endTime;
    bool        isActive;

    MaxBST      bidTree;        // custom BST — NOT std::priority_queue

    // ── Settlement fields (populated on close) ────────────────────────────
    int         winnerId;
    std::string winnerName;
    double      highestBid;
    double      settledPrice;   // Vickrey second-price
    int         totalBids;

    Auction()
        : id(0), startingPrice(0), startTime(0), endTime(0), isActive(true),
          winnerId(-1), highestBid(0), settledPrice(0), totalBids(0) {}

    // ── Serialize for the API ─────────────────────────────────────────────
    JsonObject toJson() const {
        JsonObject obj;
        obj.set("id", id)
           .set("title", title)
           .set("description", description)
           .set("imageUrl", imageUrl)
           .set("startingPrice", startingPrice)
           .set("currentHighestBid", highestBid)
           .set("totalBids", totalBids)
           .set("startTime", (long long)startTime)
           .set("endTime", (long long)endTime)
           .set("isActive", isActive);

        // Include highest bidder name if bids exist
        if (!bidTree.isEmpty()) {
            BidBSTNode* maxNode = bidTree.findMax();
            if (maxNode)
                obj.set("highestBidderName", maxNode->data.userName);
        } else {
            obj.set("highestBidderName", "");
        }

        // Include settlement info for closed auctions
        if (!isActive) {
            obj.set("winnerId", winnerId)
               .set("winnerName", winnerName)
               .set("settledPrice", settledPrice);
        }

        return obj;
    }
};

#endif // MODELS_H
