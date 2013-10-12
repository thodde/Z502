#include "list.h"

LinkedList create_list() {
    LinkedList list = (LinkedList) calloc(1, sizeof(Node));

    if (list == NULL) {
        printf("Could not create list...");
        return NULL;
    }

    list->data = NULL;
    list->next = NULL;
    return list;
}

int add_to_list( LinkedList l, PCB* p) {
    if (l->data == NULL)  {
        // The list is empty, so add to the front
        l->data = p;
        l->next = NULL;
        return 1;
    }

    if (l->data->delay > p->delay) {
        // The list is not empty, but the new entry is triggered earlier than the current head ptr
        Node *new_node = (Node*) calloc (1, sizeof(Node));
        new_node->next = l->next;
        l->next = new_node;
        new_node->data = l->data;
        l->data = p;

        return 1;
    }

    // Otherwise iterate through the list until we find the appropriate spot to nestle the new node
    Node *cursor = l;
    Node *prev = NULL;

    while (cursor != NULL && (cursor->data->delay < p->delay)) {
        prev = cursor;
        cursor = cursor->next;
    }

    //this is not an ordered list right now
    Node *new_node = (Node*) calloc (1, sizeof(Node));
    new_node->next = prev->next;
    prev->next = new_node;
    new_node->data = p;



//    LinkedList new_node = (LinkedList) calloc(1, sizeof(Node));
//
//    if (new_node == NULL) {
//        printf("Could not create node...");
//        return 0;
//    }
//
//    new_node->data = p;
//    new_node->next = l->next;
//    l->next = new_node;
    return 1;
}
