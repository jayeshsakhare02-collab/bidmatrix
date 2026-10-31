// ============================================================================
//  bst.h — Custom Binary Search Tree Data Structures for BidMatrix
// ============================================================================
//  Contains two BST variants built from scratch (no STL heaps/priority queues):
//
//  1. MaxBST  — Per-auction bid ranking. Rightmost node = highest bid.
//               Used to find the winner and Vickrey (second-highest) price.
//
//  2. MinBST  — Global auction expiry tracker. Leftmost node = soonest expiry.
//               Background thread polls this to auto-close expired auctions.
// ============================================================================

#ifndef BST_H
#define BST_H

#include <string>
#include <vector>
#include <ctime>

// ─────────────────────────────────────────────────────────────────────────────
//  BidInfo — payload stored in each MaxBST node
// ─────────────────────────────────────────────────────────────────────────────
struct BidInfo {
    int    bidId;
    int    userId;
    std::string userName;
    double amount;
    time_t timestamp;
};

// ─────────────────────────────────────────────────────────────────────────────
//  BidBSTNode — node for the per-auction Max-BST (keyed by bid amount)
// ─────────────────────────────────────────────────────────────────────────────
struct BidBSTNode {
    double      key;       // bid amount (sort key)
    BidInfo     data;      // full bid payload
    BidBSTNode* left;
    BidBSTNode* right;
    BidBSTNode* parent;

    BidBSTNode(double amount, const BidInfo& info)
        : key(amount), data(info), left(NULL), right(NULL), parent(NULL) {}
};

// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║  MaxBST — Binary Search Tree for Per-Auction Bid Management             ║
// ║                                                                         ║
// ║  • insert()        — O(log N) average insert by bid amount              ║
// ║  • findMax()       — O(log N) rightmost traversal → highest bid         ║
// ║  • findSecondMax() — in-order predecessor of max → Vickrey price        ║
// ║  • getAllBidsSorted() — in-order traversal → ascending list             ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
class MaxBST {
private:
    BidBSTNode* root;
    int         nodeCount;

    // Recursive in-order traversal (ascending order by amount)
    void inorderHelper(BidBSTNode* node, std::vector<BidInfo>& result) const {
        if (!node) return;
        inorderHelper(node->left, result);
        result.push_back(node->data);
        inorderHelper(node->right, result);
    }

    // Recursive destructor
    void destroyTree(BidBSTNode* node) {
        if (!node) return;
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }

public:
    MaxBST() : root(NULL), nodeCount(0) {}
    ~MaxBST() { destroyTree(root); }

    // ── Insert a new bid ──────────────────────────────────────────────────
    //  Bids are inserted by amount. Equal amounts go right (later bid wins tie).
    void insert(double amount, int bidId, int userId,
                const std::string& userName, time_t ts)
    {
        BidInfo info;
        info.bidId     = bidId;
        info.userId    = userId;
        info.userName  = userName;
        info.amount    = amount;
        info.timestamp = ts;

        BidBSTNode* newNode = new BidBSTNode(amount, info);

        if (!root) {
            root = newNode;
            nodeCount++;
            return;
        }

        BidBSTNode* curr = root;
        BidBSTNode* par  = NULL;

        while (curr) {
            par = curr;
            if (amount < curr->key)
                curr = curr->left;
            else
                curr = curr->right;   // >= goes right
        }

        newNode->parent = par;
        if (amount < par->key)
            par->left = newNode;
        else
            par->right = newNode;

        nodeCount++;
    }

    // ── Find the highest bid (rightmost node) ─────────────────────────────
    //  Traverses right children until a leaf is reached.  O(log N) average.
    BidBSTNode* findMax() const {
        if (!root) return NULL;
        BidBSTNode* curr = root;
        while (curr->right) curr = curr->right;
        return curr;
    }

    // ── Find the second-highest bid (Vickrey price) ───────────────────────
    //  This is the in-order predecessor of the maximum node:
    //    Case 1: Max has a left subtree → rightmost node of left subtree
    //    Case 2: Max has no left subtree → its parent
    BidBSTNode* findSecondMax() const {
        BidBSTNode* maxNode = findMax();
        if (!maxNode) return NULL;

        // Case 1: left subtree exists — rightmost of left subtree
        if (maxNode->left) {
            BidBSTNode* curr = maxNode->left;
            while (curr->right) curr = curr->right;
            return curr;
        }

        // Case 2: no left subtree — parent is the second-highest
        return maxNode->parent;   // may be NULL if only one node
    }

    // ── Get all bids sorted ascending ─────────────────────────────────────
    std::vector<BidInfo> getAllBidsSorted() const {
        std::vector<BidInfo> result;
        inorderHelper(root, result);
        return result;
    }

    int  count()   const { return nodeCount; }
    bool isEmpty() const { return root == NULL; }
};


// ─────────────────────────────────────────────────────────────────────────────
//  TimeBSTNode — node for the global Min-BST (keyed by auction close time)
// ─────────────────────────────────────────────────────────────────────────────
struct TimeBSTNode {
    time_t       closeTime;   // key — epoch seconds when auction expires
    int          auctionId;   // value — which auction this represents
    TimeBSTNode* left;
    TimeBSTNode* right;
    TimeBSTNode* parent;

    TimeBSTNode(time_t t, int id)
        : closeTime(t), auctionId(id), left(NULL), right(NULL), parent(NULL) {}
};

// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║  MinBST — Binary Search Tree for Global Auction Expiry Tracking         ║
// ║                                                                         ║
// ║  • insert()   — O(log N) insert by closing timestamp                    ║
// ║  • findMin()  — O(log N) leftmost traversal → soonest expiry           ║
// ║  • peekMin()  — read min data without removing                          ║
// ║  • removeMin()— delete the leftmost node after processing               ║
// ║  • removeByAuctionId() — cancel/remove a specific auction               ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
class MinBST {
private:
    TimeBSTNode* root;
    int          nodeCount;

    // Standard BST transplant: replace subtree u with subtree v
    void transplant(TimeBSTNode* u, TimeBSTNode* v) {
        if (!u->parent)
            root = v;
        else if (u == u->parent->left)
            u->parent->left = v;
        else
            u->parent->right = v;

        if (v) v->parent = u->parent;
    }

    // Recursive destructor
    void destroyTree(TimeBSTNode* node) {
        if (!node) return;
        destroyTree(node->left);
        destroyTree(node->right);
        delete node;
    }

    // Search for a node by auctionId (tree is keyed by time, not ID,
    // so we must do a full traversal)
    TimeBSTNode* findByAuctionId(TimeBSTNode* node, int auctionId) const {
        if (!node) return NULL;
        if (node->auctionId == auctionId) return node;
        TimeBSTNode* found = findByAuctionId(node->left, auctionId);
        if (found) return found;
        return findByAuctionId(node->right, auctionId);
    }

public:
    MinBST() : root(NULL), nodeCount(0) {}
    ~MinBST() { destroyTree(root); }

    // ── Insert an auction closing time ────────────────────────────────────
    void insert(time_t closeTime, int auctionId) {
        TimeBSTNode* newNode = new TimeBSTNode(closeTime, auctionId);

        if (!root) {
            root = newNode;
            nodeCount++;
            return;
        }

        TimeBSTNode* curr = root;
        TimeBSTNode* par  = NULL;

        while (curr) {
            par = curr;
            if (closeTime < curr->closeTime)
                curr = curr->left;
            else
                curr = curr->right;
        }

        newNode->parent = par;
        if (closeTime < par->closeTime)
            par->left = newNode;
        else
            par->right = newNode;

        nodeCount++;
    }

    // ── Find the soonest-expiring auction (leftmost node) ─────────────────
    TimeBSTNode* findMin() const {
        if (!root) return NULL;
        TimeBSTNode* curr = root;
        while (curr->left) curr = curr->left;
        return curr;
    }

    // ── Remove a specific node (standard BST deletion) ────────────────────
    void removeNode(TimeBSTNode* node) {
        if (!node) return;

        if (!node->left) {
            transplant(node, node->right);
        } else if (!node->right) {
            transplant(node, node->left);
        } else {
            // Two children: find in-order successor (leftmost of right subtree)
            TimeBSTNode* succ = node->right;
            while (succ->left) succ = succ->left;

            if (succ->parent != node) {
                transplant(succ, succ->right);
                succ->right = node->right;
                succ->right->parent = succ;
            }
            transplant(node, succ);
            succ->left = node->left;
            succ->left->parent = succ;
        }

        delete node;
        nodeCount--;
    }

    // ── Peek at the minimum without removing ──────────────────────────────
    bool peekMin(time_t& closeTime, int& auctionId) const {
        TimeBSTNode* minNode = findMin();
        if (!minNode) return false;
        closeTime  = minNode->closeTime;
        auctionId  = minNode->auctionId;
        return true;
    }

    // ── Remove the minimum node ───────────────────────────────────────────
    void removeMin() {
        TimeBSTNode* minNode = findMin();
        if (minNode) removeNode(minNode);
    }

    // ── Remove by auction ID (for cancellation) ──────────────────────────
    void removeByAuctionId(int auctionId) {
        TimeBSTNode* node = findByAuctionId(root, auctionId);
        if (node) removeNode(node);
    }

    int  count()   const { return nodeCount; }
    bool isEmpty() const { return root == NULL; }
};

#endif // BST_H
