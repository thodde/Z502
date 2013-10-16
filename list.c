#include "list.h"

/**
* Returns a linkedlist
*/
LinkedList create_list() {
    // Create the first node
    LinkedList list = (LinkedList) calloc(1, sizeof(Node));

    // In case we are out of memory, or something crazy happens...
    if (list == NULL) {
        printf("Could not create list...");
        return NULL;
    }

    // Initialize node
    list->data = NULL;
    list->next = NULL;
    return list;
}

/**
* Adds a process to a specified linked list
*/
int add_to_list( LinkedList l, PCB* p) {
    // Make sure the list exists
    if (l == NULL) {
        return 0;
    }

    if (l->data == NULL) {
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

    return 1;
}

/**
* Remove a process from the list using its pid
*/
PCB* remove_from_list(LinkedList l, INT32 pid) {
    // Make sure the specified list exists
    if (l == NULL) {
        return NULL;
    }

    // Make sure the first node actually has data in it
    if (l->data == NULL) {
        return NULL;
    }

    // Check to see if the pid we are looking for belongs to the first node in the list
    if (l->data->pid == pid) {
        PCB* returnVal = l->data;
        if (l->next == NULL) {
            //empty list
            l->data = NULL;
        }
        else {
            // We have to move some pointers around to account for the node removal
            Node *next = l->next;
            l->data = next->data;
            l->next = next->next;
            // avoid memory leaks!
            //free(l->next);
        }
        return returnVal;
    }

    Node *cursor = l;
    Node *prev = NULL;

    // If we made it here, the pid was not found in the first node
    // so we have to look through the remainder of the list
    while (cursor != NULL) {
        // If we have found the pid, remove that node
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

/**
* Return the length of the list that is passed in
*/
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

/**
* Search the list for the process whose pid
* matches that of the passed in pid.
*/
PCB* search_for_pid(LinkedList l, INT32 pid) {
    Node* cursor = l;

    // Make sure the list exists
    if (l->data == NULL) {
        return NULL;
    }

    while(cursor != NULL) {
        if (cursor->data->pid == pid) {
            return cursor->data;
        }
        cursor = cursor->next;
    }
    return NULL;
}

/**
* Search the list for the process whose name
* matches that of the passed in name.
*/
PCB* search_for_name(LinkedList l, char* name) {
    Node* cursor = l;

    // Make sure the list exists
    if (l->data == NULL) {
        return NULL;
    }

    while(cursor != NULL) {
        if (strcmp(cursor->data->name, name) == 0) {
            return cursor->data;
        }
        cursor = cursor->next;
    }
    return NULL;
}

/**
* Search the list for the first child process
* of the passed in pid.
*/
PCB* search_by_parent(LinkedList l, INT32 pid) {
    Node* cursor = l;

    // Make sure the list exists
    if (l->data == NULL) {
        return NULL;
    }

    while(cursor != NULL) {
        if (cursor->data->parent == pid) {
            return cursor->data;
        }
        cursor = cursor->next;
    }
    return NULL;
}


/**
 *  After pulling off a node we'll need to call free_ready_queue to safely return the memory
 */
LinkedList build_ready_queue(LinkedList l) {
    LinkedList ready_queue = create_list();

    Node *cursor = l;

    while (cursor != NULL) {
        if (cursor->data != NULL) {
            //TODO implied that we clear the delay after a timer has been set and fired and return by the hardware
            if ((cursor->data->state == CREATE) || (cursor->data->state == READY) || (cursor->data->state == RUNNING)) {
                add_to_list(ready_queue, cursor->data);
            }
        }
        cursor = cursor->next;
    }

    ready_queue = order_by_priority(ready_queue);

//    printf("Current Schedule: ");
//    cursor = ready_queue;
//
//    while(cursor != NULL) {
//        if (cursor->data != NULL) {
//            printf(" %i", cursor->data->pid);
//        }
//        cursor = cursor->next;
//    }
//    printf("\n");

    return ready_queue;
}

/**
 *  Is destructive to the calling list.
 */
LinkedList order_by_priority(LinkedList l) {
    LinkedList output = create_list();
    Node *original_list = l;
    Node *add_node = output;

    while (get_length(original_list) > 0) {
        Node *best_node = NULL;
        Node *cursor = original_list;

        while(cursor != NULL) {
            if (cursor->data != NULL) {
                if (best_node == NULL)
                    best_node = cursor;
                else if (best_node->data->priority > cursor->data->priority)
                    best_node = cursor;
            }
            cursor = cursor->next;
        }

        if (best_node != NULL) {
            PCB *removal = remove_from_list(original_list, best_node->data->pid);

            if (add_node->data == NULL)
                add_node->data = removal;
            else {
                add_node->next = (Node*) calloc (1, sizeof(Node));
                add_node = add_node->next;
                add_node->next = NULL;
                add_node->data = removal;
            }
        }
        else
            printf("Error!  no node was detected but the list length is greater than 1\n");

        cursor = original_list;
    }

    return output;
}

/**
 *  We need to destroy the Node* object, but leave the PCB alone
 */
void free_ready_queue(LinkedList l) {
    while (l->data != NULL)
        remove_from_list(l, l->data->pid);
}