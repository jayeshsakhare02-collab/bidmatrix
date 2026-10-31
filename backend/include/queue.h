// ============================================================================
//  queue.h — Custom FIFO Queue for BidMatrix
// ============================================================================
//  A linked-list-based FIFO queue used for:
//    • System notification / event feeds
//    • Chronological activity logs
//
//  Supports an optional max-size cap (oldest items are auto-dequeued).
// ============================================================================

#ifndef QUEUE_H
#define QUEUE_H

#include <stdexcept>
#include <vector>

template<typename T>
class Queue {
private:
    // ── Internal node ─────────────────────────────────────────────────────
    struct Node {
        T     data;
        Node* next;
        Node(const T& d) : data(d), next(NULL) {}
    };

    Node* front;
    Node* rear;
    int   queueSize;
    int   maxSize;     // -1 = unlimited

public:
    explicit Queue(int maxCap = -1)
        : front(NULL), rear(NULL), queueSize(0), maxSize(maxCap) {}

    ~Queue() {
        while (front) {
            Node* temp = front->next;
            delete front;
            front = temp;
        }
    }

    // ── Enqueue — add to the back ─────────────────────────────────────────
    //  If the queue is at capacity, the oldest (front) item is dropped.
    void enqueue(const T& data) {
        if (maxSize > 0 && queueSize >= maxSize) {
            dequeue();   // drop oldest to make room
        }

        Node* newNode = new Node(data);
        if (!rear) {
            front = rear = newNode;
        } else {
            rear->next = newNode;
            rear = newNode;
        }
        queueSize++;
    }

    // ── Dequeue — remove from the front ───────────────────────────────────
    T dequeue() {
        if (!front) throw std::runtime_error("Queue is empty");
        Node* oldFront = front;
        T data = oldFront->data;
        front = front->next;
        if (!front) rear = NULL;
        delete oldFront;
        queueSize--;
        return data;
    }

    // ── Peek — view front item without removing ───────────────────────────
    T& peek() {
        if (!front) throw std::runtime_error("Queue is empty");
        return front->data;
    }

    bool isEmpty() const { return queueSize == 0; }
    int  size()    const { return queueSize; }

    // ── Snapshot — copy all items to a vector (for JSON serialization) ────
    //  Does NOT remove items from the queue.
    std::vector<T> toVector() const {
        std::vector<T> result;
        Node* curr = front;
        while (curr) {
            result.push_back(curr->data);
            curr = curr->next;
        }
        return result;
    }
};

#endif // QUEUE_H
