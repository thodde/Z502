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
    if (l == NULL) {
        return 0;
    }

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
    if (prev == NULL) {
        new_node->next = cursor->next;
        cursor->next = new_node;
    }
    else {
        new_node->next = prev->next;
        prev->next = new_node;

    }
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

PCB* remove_from_list(LinkedList l, INT32 pid) {
    // iterate through the list until we find the appropriate spot to remove the node

    if (l == NULL) {
        return NULL;
    }

    if (l->data == NULL) {
        return NULL;
    }

    if (l->data->pid == pid) {
        PCB* returnVal = l->data;
        if (l->next == NULL) {
            //empty list
            l->data = NULL;
        }
        else {
            Node *next = l->next;
            l->data = next->data;
            l->next = next->next;
            free(l->next);
        }
        return returnVal;
    }

    Node *cursor = l;
    Node *prev = NULL;

    while (cursor != NULL) {
        if (cursor->data->pid == pid) {
            prev->next = cursor->next;
            PCB* returnVal = cursor->data;
            free(cursor);

            return returnVal;
        }

        prev = cursor;
        cursor = cursor->next;
    }
    return NULL;
}

int get_length(LinkedList l) {
    int length = 0;
    Node *cursor = l;

    if (cursor->data == NULL)
        return length;

    while( cursor != NULL ){
        cursor = cursor->next;
        length++;
    }

    return length;
}

Node* search_for_pid(LinkedList l, INT32 pid) {
    Node* cursor = l;

    if (l->data == NULL) {
        return NULL;
    }

    while(cursor != NULL) {
        if (cursor->data->pid == pid) {
            return cursor;
        }
        cursor = cursor->next;
    }
    return NULL;
}

Node* search_for_name(LinkedList l, char* name) {
    Node* cursor = l;

    if (l->data == NULL) {
        return NULL;
    }

    while(cursor != NULL) {
        if (strcmp(cursor->data->name, name) == 0) {
            return cursor;
        }
        cursor = cursor->next;
    }
    return NULL;
}