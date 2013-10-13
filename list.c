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
        //worthelss updates
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

BOOL remove_from_list(LinkedList l, PCB* p) {
    // iterate through the list until we find the appropriate spot to remove the node
    Node *cursor = l;
    Node *prev = NULL;

    while (cursor != NULL && (cursor->data->pid != p->pid)) {
        prev = cursor;
        cursor = cursor->next;

        if(cursor->data->pid == p->pid) {
            prev->next = cursor->next;
            free(cursor);
            return 1;
        }
    }
    return 0;
}

int get_length(LinkedList l) {
    int length = 0;
    Node *cursor = l;
    Node *prev = NULL;

    while( cursor != NULL ){
        cursor = cursor->next;
        length++;
    }

    return length;
}
