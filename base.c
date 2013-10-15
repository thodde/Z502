/************************************************************************

        This code forms the base of the operating system you will
        build.  It has only the barest rudiments of what you will
        eventually construct; yet it contains the interfaces that
        allow test.c and z502.c to be successfully built together.

        Revision History:
        1.0 August 1990
        1.1 December 1990: Portability attempted.
        1.3 July     1992: More Portability enhancements.
                           Add call to sample_code.
        1.4 December 1992: Limit (temporarily) printout in
                           interrupt handler.  More portability.
        2.0 January  2000: A number of small changes.
        2.1 May      2001: Bug fixes and clear STAT_VECTOR
        2.2 July     2002: Make code appropriate for undergrads.
                           Default program start is in test0.
        3.0 August   2004: Modified to support memory mapped IO
        3.1 August   2004: hardware interrupt runs on separate thread
        3.11 August  2004: Support for OS level locking
	4.0  July    2013: Major portions rewritten to support multiple threads
************************************************************************/

#include             "global.h"
#include             "syscalls.h"
#include             "protos.h"
#include             "string.h"
#include             "my_globals.h"
#include             "list.h"


extern INT16 Z502_MODE;

// These locations are global and define information about the page table
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;

extern void          *TO_VECTOR [];


// for keeping track of the current pid
INT32 gen_pid = 1;
PCB                *current_PCB = NULL;    // this is the currently running PCB
PCB                *root_process_pcb = NULL;
LinkedList         timer_queue;            // Holds all processes that are currently waiting for the timer queue
LinkedList         ready_queue;            // Holds all processes that are currently waiting to be run
LinkedList         process_list;           // Holds all processes that exist

int                total_timer_pid = 0;    //counter for the number of PCBs in the timer queue


BOOL interrupt_lock = TRUE;

char *call_names[] = { "mem_read ", "mem_write",
                       "read_mod ", "get_time ", "sleep    ",
                       "get_pid  ", "create   ", "term_proc",
                       "suspend  ", "resume   ", "ch_prior ",
                       "send     ", "receive  ", "disk_read",
                       "disk_wrt ", "def_sh_ar" };

/************************************************************************
    INTERRUPT_HANDLER
        When the Z502 gets a hardware interrupt, it transfers control to
        this routine in the OS.
************************************************************************/
void    interrupt_handler( void ) {
    INT32              device_id;
    INT32              status;
    INT32              Index = 0;
    INT32              Time;

    printf("INTERRUPT HANDLER GOT CALLED!!\n");

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

    switch(device_id) {
        case(TIMER_INTERRUPT):
            printf("Timer interrupt\n");
            MEM_READ(Z502ClockStatus, &Time);

            if (timer_queue == NULL)
                printf("Error!  no PCBs are on the timer queue\n");
            else {
                if (timer_queue->data == NULL) {
                    printf("Error!  no PCBs are on the timer queue\n");
                }
                else {
                    PCB* waking_process = remove_from_list(timer_queue, timer_queue->data->pid);
                    printf("Waking process: %s\n", waking_process->name);
                    waking_process->state = READY;
                }
            }

            // Remove current_PCB from timer queue
            interrupt_lock = FALSE;

            break;
    }

    // Clear out this device - we're done with it
    MEM_WRITE(Z502InterruptClear, &Index );
}                                       /* End of interrupt_handler */

/************************************************************************
    FAULT_HANDLER
        The beginning of the OS502.  Used to receive hardware faults.
************************************************************************/

void    fault_handler( void )
    {
    INT32       device_id;
    INT32       status;
    INT32       Index = 0;

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

    printf( "Fault_handler: Found vector type %d with value %d\n",
                        device_id, status );
    // Clear out this device - we're done with it
    MEM_WRITE(Z502InterruptClear, &Index );
}                                       /* End of fault_handler */

/************************************************************************
    SVC
        The beginning of the OS502.  Used to receive software interrupts.
        All system calls come to this point in the code and are to be
        handled by the student written code here.
        The variable do_print is designed to print out the data for the
        incoming calls, but does so only for the first ten calls.  This
        allows the user to see what's happening, but doesn't overwhelm
        with the amount of data.
************************************************************************/

void    svc( SYSTEM_CALL_DATA *SystemCallData ) {
    short               call_type;
    static short        do_print = 10;
    short               i;
    INT32               current_time;
    char                *name;
    void                *addr;
    int                 priority;
    PCB*                process_handle;
    Node*               process_node;
    INT32               tmp_pid;

    call_type = (short)SystemCallData->SystemCallNumber;
    if ( do_print > 0 ) {
        printf( "SVC handler: %s\n", call_names[call_type]);
        for (i = 0; i < SystemCallData->NumberOfArguments - 1; i++ ){
        	 //Value = (long)*SystemCallData->Argument[i];
             printf( "Arg %d: Contents = (Decimal) %8ld,  (Hex) %8lX\n", i,
             (unsigned long )SystemCallData->Argument[i],
             (unsigned long )SystemCallData->Argument[i]);
        }
        do_print--;
    }

    switch(call_type) {
        case SYSNUM_GET_TIME_OF_DAY:
            MEM_READ(Z502ClockStatus, SystemCallData->Argument[0]);
            break;

        case SYSNUM_TERMINATE_PROCESS:
            if (SystemCallData->Argument[0] == -1) {
                *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                current_PCB->state = TERMINATE;
                switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
            }
            else if (SystemCallData->Argument[0] == -2) {            //kill self and all of children
                PCB* process_pcb = search_for_pid(process_list, current_PCB->pid);
                if(process_pcb != NULL) {
                    *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                    process_pcb->state = TERMINATE;
                    pcb_cascade_delete_by_parent(process_pcb->pid);
                    switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
                }
                else {
                    *(SystemCallData->Argument[1]) = ERR_BAD_PARAM;  // If the process was not found, return an error
                }
            }
            else {
                process_handle = search_for_pid(process_list, SystemCallData->Argument[0]);

                // If we found the process, destroy it
                if(process_handle != NULL) {
                    *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                    process_handle->state = TERMINATE;

                    //TODO this should possibly go to the process_handler more often than just if you are killing the root process
                    //if (process_handle->pid == root_process_pcb->pid) {
                    switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
                    //}
                }
                else {
                    // If the process was not found, return an error
                    *(SystemCallData->Argument[1]) = ERR_BAD_PARAM;
                }
            }
            // TODO really I should just switch to the root process only if the current process is killed, because a true scheduler
            // will delete the processes the next time there is a switch.  But as this isn't ready yet, I need to put in
            // checks to see if this is needed.
            break;

        case SYSNUM_SLEEP:
            MEM_READ( Z502TimerStatus, &current_time);
            current_PCB->delay = SystemCallData->Argument[0];
            start_timer();
            break;

        case SYSNUM_CREATE_PROCESS:
            name = (char *)SystemCallData->Argument[0];
            addr = (void*) SystemCallData->Argument[1];
            priority = (int)SystemCallData->Argument[2];

            if (addr == NULL) {
                printf("Error, function handle not recognized: %s\n", SystemCallData->Argument[1]);
                *(SystemCallData->Argument[4]) = ERR_BAD_PARAM;
            }
            else {
                process_handle = os_make_process(name, priority, SystemCallData->Argument[4], addr, KERNEL_MODE);

                if(process_handle != NULL) {
                    *(SystemCallData->Argument[4]) = ERR_SUCCESS;
                    *(SystemCallData->Argument[3]) = process_handle->pid;
                }
                else
                    *(SystemCallData->Argument[4]) = ERR_BAD_PARAM;
            }


            break;

        case SYSNUM_GET_PROCESS_ID:
            name = (char *)SystemCallData->Argument[0];

            if (strcmp(name, "") == 0) {
                //return pid of calling process
                *(SystemCallData->Argument[1]) = current_PCB->pid;
                *(SystemCallData->Argument[2]) = ERR_SUCCESS;
            }
            else {
                // search the process queue for the process id
                process_handle = search_for_name(process_list, name);

                // we got it!
                if (process_handle != NULL) {
                    *(SystemCallData->Argument[1]) = process_handle->pid;
                    *(SystemCallData->Argument[2]) = ERR_SUCCESS;
                }
                else {
                    // no matching process name found
                    *(SystemCallData->Argument[2]) = ERR_BAD_PARAM;
                }
            }
            break;

        case SYSNUM_SUSPEND_PROCESS:
            tmp_pid = (int*)SystemCallData->Argument[0];
            process_handle = search_for_pid(process_list, tmp_pid);

            // TODO: More has to happen here -- we have to talk to the dispatcher and make sure
            // the schedule is updated and whatnot...


            // Make sure we got a valid process
            if(process_handle != NULL) {
                // Is the process running?
                if(process_handle->state == RUNNING) {
                    // Throw error
                    *(SystemCallData->Argument[1]) = ERR_BAD_PARAM;
                }
                // Is the process already suspended?
                else if(process_handle->state == SUSPEND) {
                    //Throw error
                    *(SystemCallData->Argument[1]) = ERR_BAD_PARAM;
                }
                // We can finally suspend the process
                else {
                    process_handle->state = SUSPEND;
                    remove_from_list(ready_queue, process_handle->pid);
                    *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                }
            }
            else {
                // The process does not exist
                *SystemCallData->Argument[1] = ERR_BAD_PARAM;
            }
            break;

        case SYSNUM_RESUME_PROCESS:
            // TODO: Add dispatcher and scheduler stuff

            tmp_pid = (int*)SystemCallData->Argument[0];
            process_handle = search_for_pid(process_list, tmp_pid);

            // Make sure we got a valid process
            if(process_handle != NULL) {
                // Is the process running?
                if(process_handle->state == SUSPEND) {
                    // Throw error
                    *(SystemCallData->Argument[1]) = ERR_BAD_PARAM;
                }
                else {
                    process_handle->state = READY;
                    add_to_list(ready_queue, process_handle);
                    *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                }
            }
            else {
                // The process does not exist
                *SystemCallData->Argument[1] = ERR_BAD_PARAM;
            }

            break;

        default:
            printf("Unrecognized system call!!\n");
    }
}                                               // End of svc

/************************************************************************
    osInit
        This is the first routine called after the simulation begins.  This
        is equivalent to boot code.  All the initial OS components can be
        defined and initialized here.
************************************************************************/

void    osInit( int argc, char *argv[]  ) {
    INT32 error_response;
    void  *next_context;
    INT32 i;

    PCB* test_process;
    timer_queue = create_list();
    process_list = create_list();

    root_process_pcb = os_make_process("root", DEFAULT_PRIORITY, &error_response, (void*) dispatcher, KERNEL_MODE);

    /* Demonstrates how calling arguments are passed thru to here       */

    printf( "Program called with %d arguments:", argc );
    for ( i = 0; i < argc; i++ )
        printf( " %s", argv[i] );
    printf( "\n" );
    printf( "Calling with argument 'sample' executes the sample program.\n" );

    /*          Setup so handlers will come to code in base.c           */

    TO_VECTOR[TO_VECTOR_INT_HANDLER_ADDR]   = (void *)interrupt_handler;
    TO_VECTOR[TO_VECTOR_FAULT_HANDLER_ADDR] = (void *)fault_handler;
    TO_VECTOR[TO_VECTOR_TRAP_HANDLER_ADDR]  = (void *)svc;

    /*  Determine if the switch was set, and if so go to demo routine.  */

    if (( argc > 1 ) && ( strcmp( argv[1], "sample" ) == 0 ) ) {
        test_process = os_make_process(argv[1], DEFAULT_PRIORITY, &error_response, (void*) sample_code, KERNEL_MODE);
        switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
    }                   /* This routine should never return!!           */
    else {
        void* func;
        func = (void*) get_function_handle(argv[1]);
        printf("Function: %s address1 : (%ld, %ld) address2: (%ld, %ld)\n", argv[1], &func, func, &test1a, test1a);

        if (func != NULL) {
                test_process = os_make_process(argv[1], DEFAULT_PRIORITY, &error_response, func, KERNEL_MODE);
                switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
        }
        else {
            printf("Error!  Unrecognized entry point!\n");
        }
    }
}                                               // End of osInit

PCB* os_make_process(char* name, INT32 priority, INT32* error, void* entry_point, INT32 mode) {
    if ((priority < MIN_PRIORITY) || (priority > MAX_PRIORITY)) {
        *error = ERR_BAD_PARAM;
        return NULL;
    }

    if (get_length(process_list) >= MAX_PROCESSES) {
        printf("Reached maximum number of processes\n");
        *error = ERR_BAD_PARAM;
        return NULL;
    }

    PCB* process_handle = search_for_name(process_list, name);
    if (process_handle != NULL) {
        *error = ERR_BAD_PARAM;
        return NULL;
    }

    PCB* pcb = (PCB*) calloc(1, sizeof(PCB));       // allocate memory for PCB

    pcb->delay = 0;                                 // start time = now (zero)
    pcb->pid = gen_pid;                             // assign pid
    gen_pid++;
    pcb->state=CREATE;

    memset(pcb->name, 0, MAX_NAME);                 // assign process name
    strcpy(pcb->name, name);                        // assign process name

    if (current_PCB != NULL)
        pcb->parent = current_PCB->pid;             // assign parent id
    else if (root_process_pcb != NULL)
        pcb->parent = root_process_pcb->pid;        // no current process?  put it under the root process
    else
        pcb->parent = -1;                           // -1 means this process is the parent process

    (*error) = ERR_SUCCESS;                         // return error value

    if (pcb->parent != -1)                          // Add everything except the root process to the process_list
        add_to_list(process_list, pcb);

    Z502MakeContext(&pcb->context, entry_point, mode );

    return pcb;
}

// Used for removing unneeded processes that have been marked for termination
void os_destroy_process(PCB* pcb) {
    if (current_PCB->pid != root_process_pcb->pid) { //only the root can destroy processes
        printf("error, only root can destroy processes\n");
        return;
    }

    Z502DestroyContext(&pcb->context);
    free(pcb);
}

/**
 *   This function is responsible for removing processes
 *  from the ready queue and switching to their context
 *  when they are available. It also adds processes
 *  to the timer queue when they are sleeping
 */
void dispatcher() {
    int i = 0;
    while (TRUE) {
        i++;
        if (i > 10) {
            printf("limiting to %i iterations before forced quit\n", i);
            Z502Halt();
        }

        // Check for terminated processes.
        Node *cursor = process_list;
        while (cursor != NULL) {
            if (cursor->data != NULL) {
                if (cursor->data->state == TERMINATE) {
                    PCB* dead_process = remove_from_list(process_list, cursor->data->pid);
                    os_destroy_process(dead_process);
                    cursor = process_list;
                }
                else
                    cursor = cursor->next;
            }
            else
                cursor = cursor->next;
        }

        if (get_length(process_list) == 0) {        //If no active processes then halt
            printf("No processes exist other than root, halting\n");
            Z502Halt();
        }
        else
            printf("there are %i processes\n", get_length(process_list));

        if (root_process_pcb->state == TERMINATE) {
            printf("Root processed killed.  halting\n");
            Z502Halt();
        }

        ready_queue = build_ready_queue(process_list);

        if(ready_queue != NULL) {
            if(ready_queue->data != NULL) {
                PCB* process_to_run = ready_queue->data;
                process_to_run->state = RUNNING;
                free_ready_queue(ready_queue);
                printf("found live node, switching to process '%s' with pid '%i'\n", process_to_run->name, process_to_run->pid);
                switch_context(process_to_run, SWITCH_CONTEXT_SAVE_MODE);
            }
            else {
                printf("no nodes are available to run...sleeping\n");
                free_ready_queue(ready_queue);
                Z502Idle();
            }
        }
        else
            Z502Idle();
    }
    printf("Error, I should never ever ever get here\n");
    Z502Halt();
}

/*********************************************************
 * Switches contexts for the current PCB
**********************************************************/
void switch_context( PCB* pcb, short context_mode) {
    if (current_PCB != NULL) {
        if (current_PCB->state == RUNNING)
            current_PCB->state = READY;
    }
	current_PCB = pcb;
    current_PCB -> state = RUNNING;      //update the PCB state to RUN

    Z502SwitchContext( context_mode, &(pcb->context));
}

/**
 *   Recursively find all children and mark them for termination
 */
void pcb_cascade_delete_by_parent(INT32 parent_pid) {
    Node *cursor = process_list;
    while (cursor != NULL) {
        if (cursor->data != NULL) {
            if(cursor->data->parent == parent_pid) {
                cursor->data->state = TERMINATE;
                pcb_cascade_delete_by_parent(cursor->data->pid);
            }
        }

        cursor = cursor->next;
    }
}

void start_timer() {
    INT32 status;
    add_to_list(timer_queue, current_PCB);
    current_PCB->state = SLEEPING;
    MEM_WRITE(Z502TimerStart, &current_PCB->delay);
}

// This will be needed later
void wait_for_interrupt() {

}

func_ptr get_function_handle(char *name) {
    printf("looking for function handle with name: %s\n", name);
    func_ptr response;
    if (strcmp( name, "sample" ) == 0 )
        response = (void*) sample_code;
    else if ( strcmp( name, "test0" ) == 0 )
        response = (void*) test0;
    else if ( strcmp( name, "test1a" ) == 0 )
        response = (void*) test1a;
    else if ( strcmp( name, "test1b" ) == 0 )
        response = (void*) test1b;
    else if ( strcmp( name, "test1c" ) == 0 )
        response = (void*) test1c;
    else if ( strcmp( name, "test1d" ) == 0 )
        response = (void*) test1d;
    else if ( strcmp( name, "test1e" ) == 0 )
        response = (void*) test1e;
    else if ( strcmp( name, "test1f" ) == 0 )
        response = (void*) test1f;
    else
        response = NULL;
    return response;
}
