// ============================================================================
//  linked_list.h — Custom Singly-Linked List for BidMatrix
// ============================================================================
//  A generic singly-linked list used for:
//    • Per-user bid history tracking
//    • Ordered event storage
//
//  Demonstrates manual linked-list management without STL containers.
// ============================================================================

#ifndef LINKED_LIST_H
#define LINKED_LIST_H

#include <stdexcept>

template<typename T>
class LinkedList {
private:
    // ── Internal node ─────────────────────────────────────────────────────
    struct Node {
        T     data;
        Node* next;
        Node(const T& d) : data(d), next(NULL) {}
    };

    Node* head;
    Node* tail;
    int   listSize;

public:
    LinkedList() : head(NULL), tail(NULL), listSize(0) {}

    ~LinkedList() {
        Node* curr = head;
        while (curr) {
            Node* temp = curr->next;
            delete curr;
            curr = temp;
        }
    }

    // ── Append to the end of the list — O(1) ─────────────────────────────
    void append(const T& data) {
        Node* newNode = new Node(data);
        if (!tail) {
            head = tail = newNode;
        } else {
            tail->next = newNode;
            tail = newNode;
        }
        listSize++;
    }

    // ── Access by index — O(N) ────────────────────────────────────────────
    T& get(int index) {
        if (index < 0 || index >= listSize)
            throw std::out_of_range("LinkedList index out of range");
        Node* curr = head;
        for (int i = 0; i < index; i++) curr = curr->next;
        return curr->data;
    }

    const T& get(int index) const {
        if (index < 0 || index >= listSize)
            throw std::out_of_range("LinkedList index out of range");
        Node* curr = head;
        for (int i = 0; i < index; i++) curr = curr->next;
        return curr->data;
    }

    // ── Size and emptiness ────────────────────────────────────────────────
    int  size()    const { return listSize; }
    bool isEmpty() const { return listSize == 0; }

    // ── Simple forward iterator ───────────────────────────────────────────
    class Iterator {
        Node* curr;
    public:
        Iterator(Node* n) : curr(n) {}
        T& operator*()             { return curr->data; }
        Iterator& operator++()     { curr = curr->next; return *this; }
        bool operator!=(const Iterator& other) const { return curr != other.curr; }
    };

    Iterator begin() { return Iterator(head); }
    Iterator end()   { return Iterator(NULL); }
};

#endif // LINKED_LIST_H
