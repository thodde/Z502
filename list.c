#include "list.h"

LinkedList create_list() {
    LinkedList list = (LinkedList) calloc(1, sizeof(Node));

    if (list == NULL) {
        printf("Could not create list...");
        return NULL;
    }

    list->next = NULL;
    return list;
}

int add_to_list( LinkedList l, PCB* p) {
    LinkedList new_node = (LinkedList) calloc(1, sizeof(Node));
    
    if (new_node == NULL) {
        printf("Could not create node...");
        return 0;
    }

    new_node->data = p;
    new_node->next = l->next;
    l->next = new_node;
    return 1;
}
