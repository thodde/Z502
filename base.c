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

// define information about the page table
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;

extern void          *TO_VECTOR [];

UINT16 virtual_address;

// for keeping track of the current pid
INT32 gen_pid = 1;
PCB                *current_PCB = NULL;    // this is the currently running PCB
PCB                *root_process_pcb = NULL;
LinkedList         timer_queue;            // Holds all processes that are currently waiting for the timer queue
LinkedList         process_list;           // Holds all processes that exist

PCB**              disk_queue;             // Holds all processes trying to use the disk
FRAME*             frame_list;
SHADOW_TABLE*     shadow_table;

int                total_timer_pid = 0;    //counter for the number of PCBs in the timer queue
INT32              last_context_switch = 0;  // the number of ticks since the last context switch

BOOL add_next_to_timer = FALSE;
BOOL interrupt_lock = FALSE;

// if these flags are set to 1, print out state information
int print_schedule = 0;
int print_memory = 0;

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
    INT32              lock_result;
    INT32              disk_status;

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );

    while(device_id != -1) {
        // Set this device as target of our query
        MEM_WRITE(Z502InterruptDevice, &device_id );
        // Now read the status of this device
        MEM_READ(Z502InterruptStatus, &status );

        switch(device_id) {
            case(TIMER_INTERRUPT):
                MEM_READ(Z502ClockStatus, &Time);

                if (timer_queue == NULL) {
                    break;
                }

                if (timer_queue->data == NULL) {
                    break;
                }
                PCB* waking_process = remove_from_list(timer_queue, timer_queue->data->pid);
                waking_process->state = READY;

                //add the next one to the queue
                if (get_length(timer_queue) == 0) {
                    interrupt_lock = FALSE;
                }

                if (get_length(timer_queue) > 0) {
                    add_next_to_timer = TRUE;
                }
                break;

            case(DISK_INTERRUPT):
            case(DISK_INTERRUPT+1):
            case(DISK_INTERRUPT+2):
            case(DISK_INTERRUPT+3):
            case(DISK_INTERRUPT+4):
            case(DISK_INTERRUPT+5):
            case(DISK_INTERRUPT+6):
            case(DISK_INTERRUPT+7):
            case(DISK_INTERRUPT+8):
            case(DISK_INTERRUPT+9):
            case(DISK_INTERRUPT+10):
            case(DISK_INTERRUPT+11):
            {
                // make sure the disk exists
                int disk_id = device_id - DISK_INTERRUPT + 1;
                if (device_id < DISK_INTERRUPT || device_id >= (DISK_INTERRUPT + MAX_NUMBER_OF_DISKS)) {
                    // Make sure the interrupt is valid
                    break;
                }

                INT32 disk_status;
                MEM_WRITE(Z502DiskSetID, &(disk_id));
                MEM_READ(Z502DiskStatus, &disk_status);

                if (disk_status == DEVICE_FREE) {
                    if (disk_queue[disk_id] != NULL) {
                        //I believe this is automatically done
                        disk_queue[disk_id]->state = READY;
                        disk_queue[disk_id] = NULL;
                    }
                    else {
                        printf("Error, no PCB is available for this request\n");
                    }
                }
                else
                    printf("Write is not complete yet\n");

            }
            break;

            default:
                printf("Unrecognized interrupt %i\n", device_id);
                break;
        }

        MEM_WRITE(Z502InterruptClear, &Index );
        Index += 1;
        MEM_READ(Z502InterruptDevice, &device_id );
    }

    // Clear out this device - we're done with it
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
    INT32       frame = -1;
    INT32       frame_id;
    INT32       new_frame = 0;
    INT32       lock_result;
    int         out_of_frames = 0;
    INT32       i;

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

    printf( "Fault_handler: Found vector type %d with value %d\n", device_id, status );

    switch(device_id) {
        case PRIVILEGED_INSTRUCTION:
            printf("Error!  Requesting privileged command while in USER mode\n");
            Z502Halt();
            break;
        case INVALID_MEMORY:
            // Make sure the page requested is valid
            if(status >= VIRTUAL_MEM_PGS) {
                Z502Halt();
            }

            // if the table has not been created yet, create it
            if(Z502_PAGE_TBL_LENGTH == 0) {
                // initialize the page table
                Z502_PAGE_TBL_LENGTH = VIRTUAL_MEM_PGS;
                Z502_PAGE_TBL_ADDR = (UINT16*) calloc(sizeof(UINT16), Z502_PAGE_TBL_LENGTH);

                // allocate the frame list
                frame_list = (FRAME*) calloc(sizeof(FRAME), PHYS_MEM_PGS);
                shadow_table = (SHADOW_TABLE*) calloc(sizeof(SHADOW_TABLE), PHYS_MEM_PGS);

                // init the frame list
                for(i = 0; i < (int) PHYS_MEM_PGS; i++) {
                    frame_list[i].frame_id = i;
                    frame_list[i].page_id = -1;
                    frame_list[i].pid = current_PCB->pid;
                    frame_list[i].in_use = FALSE;

                    shadow_table[i].frame_id = i;
                    shadow_table[i].page_id = -1;
                    shadow_table[i].disk_id = -1;
                    shadow_table[i].in_use = FALSE;
                }
            }

            if(out_of_frames == 0) {
                if(Z502_PAGE_TBL_ADDR[status] == NULL) {
                    // The user is requesting a page that has not yet been created
                    frame = find_empty_frame(status);

                    //make sure we are aware that we are out of memory
                    if(frame == PHYS_MEM_PGS) {
                        out_of_frames = 1;
                    }

                    if(frame == -1) {
                        //TODO: FIX THIS LATER
                        Z502Halt();
                    }
                    else {
                        // make sure the frame is not previously used
                        frame_id = (UINT16) frame_list[frame].frame_id;
                        Z502_PAGE_TBL_ADDR[status] = frame_id | PTBL_VALID_BIT;

                        // handles locking for the current thread on this chunk of memory
                        READ_MODIFY(MEMORY_INTERLOCK_BASE + frame, DO_LOCK, DO_NOT_SUSPEND, &lock_result);
                        if (lock_result == FALSE)
                            printf("Could not obtain lock!!\n");
                    }
                }
                else if(!(Z502_PAGE_TBL_ADDR[status] & PTBL_VALID_BIT)) {
                    // The requested page is invalid and is not in physical memory
                    printf("This is not a valid page.\n");
                }
                else {
                    printf("Catch all!\n");
                }
            }

            // this means all the frames have been used
            if(out_of_frames == 1) {
                // the replacement algorithm can go here
                new_frame = page_replacement();

                Z502_PAGE_TBL_ADDR[status] = (UINT16) frame_list[new_frame].frame_id | PTBL_VALID_BIT;
                frame_list[new_frame].page_id = status;
                frame_list[new_frame].pid = current_PCB->pid;
                frame_list[new_frame].in_use = TRUE;

                // handles locking for the current thread on this chunk of memory
                READ_MODIFY(MEMORY_INTERLOCK_BASE + new_frame, DO_LOCK, DO_NOT_SUSPEND, &lock_result);
                if (lock_result == FALSE)
                    printf("Error, could not obtain lock!!\n");
            }

            memory_printer();

            break;
        default:
            printf("Unrecognized device handler: %d\n", device_id);
            //Z502Halt();
            break;
    }

    if(device_id == 4 && status == 0) {
        printf("Unrecognized device handler\n");
        Z502Halt();
    }

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
    INT32               lock_result;
    INT32               disk_status;
    INT32               disk_action;
    INT32               disk_start;


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

                    // TODO this should possibly go to the process_handler more often than just if you are killing the root process
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
            printf("sleeping process: %i\n", current_PCB->pid);
            sleep_process(SystemCallData->Argument[0], current_PCB);
            switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
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
                process_handle = os_make_process(name, priority, SystemCallData->Argument[4], addr, USER_MODE);

                if(process_handle != NULL) {
                    *(SystemCallData->Argument[4]) = ERR_SUCCESS;
                    *(SystemCallData->Argument[3]) = process_handle->pid;
                    scheduler_printer("NEW");
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
                    current_PCB->suspend_reason = WAITING_UNDEFINED;
                    scheduler_printer("SUSPEND");
                    //not needed, the ready queue is built each time the dispatcher is called
                    //remove_from_list(ready_queue, process_handle->pid);
                    *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                    //TODO what if you are suspending the current process?
                }
            }
            else {
                // The process does not exist
                *SystemCallData->Argument[1] = ERR_BAD_PARAM;
            }

            break;

        case SYSNUM_RESUME_PROCESS:
            tmp_pid = (int*)SystemCallData->Argument[0];
            process_handle = search_for_pid(process_list, tmp_pid);

            // Make sure we got a valid process
            if(process_handle != NULL) {
                // Is the process running?
                if(process_handle->state != SUSPEND) {
                    // Throw error
                    *(SystemCallData->Argument[1]) = ERR_BAD_PARAM;
                }
                else {
                    process_handle->state = READY;
                    scheduler_printer("READY");
                    *(SystemCallData->Argument[1]) = ERR_SUCCESS;
                }
            }
            else {
                // The process does not exist
                *SystemCallData->Argument[1] = ERR_BAD_PARAM;
            }

            break;

        case SYSNUM_CHANGE_PRIORITY:
            tmp_pid = (int*)SystemCallData->Argument[0];
            INT32 new_priority = (int*)SystemCallData->Argument[1];

            if (tmp_pid == -1)
                process_handle = current_PCB;
            else
                process_handle = search_for_pid(process_list, tmp_pid);

            if (process_handle == NULL) {
                *SystemCallData->Argument[2] = ERR_BAD_PARAM;
            }
            else if (new_priority < MIN_PRIORITY || new_priority > MAX_PRIORITY) {
                *SystemCallData->Argument[2] = ERR_BAD_PARAM;
            }
            else {
                process_handle->priority = new_priority;
                *(SystemCallData->Argument[2]) = ERR_SUCCESS;
            }

            break;

        case SYSNUM_SEND_MESSAGE:
            {
                //            INT32 target_pid;
                //            char message_buffer[N];
                //            INT32 message_send_length;
                //            INT32 error;

                tmp_pid = (int*)SystemCallData->Argument[0];
                INT32 message_length = (INT32) SystemCallData->Argument[2];

                MESSAGE *msg = (MESSAGE*) calloc(1, sizeof(MESSAGE));
                msg->handled = FALSE;
                msg->source_pid = current_PCB->pid;
                msg->length = message_length;
                memcpy(msg->msg_buffer, SystemCallData->Argument[1], msg->length);

                if (message_length < 0 || message_length > MAX_MSG) {
                    printf("Error, message size is too large\n");
                    *SystemCallData->Argument[3] = ERR_BAD_PARAM;
                    free(msg);
                }
                else if (tmp_pid == -1) {    // Broadcast message
                    BOOL able_to_process = FALSE;
                    msg->broadcast_message = TRUE;
                    Node *cursor = process_list;
                    while (cursor != NULL) {
                        if (cursor->data != NULL) {
//                            if (cursor->data->pid != current_PCB->pid) {
                                if(enqueue_message(cursor->data, msg))
                                    able_to_process = TRUE;

                                if ((cursor->data->state == SUSPEND) && (cursor->data->suspend_reason == WAITING_FOR_MESSAGE))
                                    cursor->data->state = READY;              //wake it up!
//                            }
                        }

                        cursor = cursor->next;
                    }

                    if (able_to_process)
                        *SystemCallData->Argument[3] = ERR_SUCCESS;
                    else
                        *SystemCallData->Argument[3] = ERR_BAD_PARAM;
                }
                else {
                    process_handle = search_for_pid(process_list, tmp_pid);

                    if (process_handle == NULL)
                        *SystemCallData->Argument[3] = ERR_BAD_PARAM;
                    else if ((message_length < 0) || (message_length > MAX_MSG))
                        *SystemCallData->Argument[3] = ERR_BAD_PARAM;
                    else {
                        if (enqueue_message(process_handle, msg)) {
                            if ((process_handle->state == SUSPEND) && (process_handle->suspend_reason == WAITING_FOR_MESSAGE))
                                process_handle->state = READY;              //wake it up!
                            *SystemCallData->Argument[3] = ERR_SUCCESS;
                        }
                        else {
                            printf("Error, could not enqueue message at recipient.  Too many messages stored\n");
                            *SystemCallData->Argument[3] = ERR_BAD_PARAM;
                            free(msg);
                        }
                    }
                }
            }

            break;

        case SYSNUM_RECEIVE_MESSAGE:
            {
                tmp_pid = (int*)SystemCallData->Argument[0];
                MESSAGE *msg = NULL;
                INT32 able_to_receive_length = (INT32) SystemCallData->Argument[2];

                if ((tmp_pid != -1) && (search_for_pid(process_list, tmp_pid) == NULL)) {
                    printf("Error, process %i does not exist\n", tmp_pid);
                    *SystemCallData->Argument[5] = ERR_BAD_PARAM;
                }
                else if (able_to_receive_length > MAX_MSG || able_to_receive_length < 0) {
                    printf("Error, message receive length (%i) is either too large or too small\n", able_to_receive_length);
                    *SystemCallData->Argument[5] = ERR_BAD_PARAM;
                }
                else {
                    int message_index = find_message_by_source(current_PCB, tmp_pid);
                    while (message_index == -1) {
                        printf("No messages available from process: %i.  Sleeping\n", tmp_pid);
                        current_PCB->state = SUSPEND;
                        current_PCB->suspend_reason = WAITING_FOR_MESSAGE;
                        switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
                        message_index = find_message_by_source(current_PCB, tmp_pid);
                    }

                    msg = remove_message(current_PCB, message_index);
                    if (able_to_receive_length < msg->length) {
                        *SystemCallData->Argument[5] = ERR_BAD_PARAM;
                    }
                    else {
                        memcpy(SystemCallData->Argument[1], msg->msg_buffer, msg->length);

                        *(SystemCallData->Argument[3]) = msg->length;
                        *(SystemCallData->Argument[4]) = msg->source_pid;
                        *SystemCallData->Argument[5] = ERR_SUCCESS;
                    }

                    if (msg->broadcast_message) {
                        msg->handled = TRUE;
                        clear_handled_broadcast_message();
                    }
                    free(msg);
                }
            }

            break;

        case SYSNUM_DISK_READ:
            // make sure the disk number is valid
            if (SystemCallData->Argument[0] < 1 || SystemCallData->Argument[0] > MAX_NUMBER_OF_DISKS) {
                printf("Error, call to an invalid disk number\n");
                break;
            }

            // make sure the sector is valid
            if (SystemCallData->Argument[1] < 0 || SystemCallData->Argument[1] > NUM_LOGICAL_SECTORS) {
                printf("Error, call to an invalid sector requested\n");
                break;
            }

            // allocate memory for the disk queue
            if (disk_queue == NULL) {
                disk_queue = (PCB**) calloc(sizeof(PCB*), MAX_NUMBER_OF_DISKS);
            }

            // allocate the disk space on the current process
            if(current_PCB->disk_data == NULL)
                current_PCB->disk_data = calloc(sizeof(DISK*), 1);

            // store the disk data on the process
            current_PCB->disk_data->disk_id = SystemCallData->Argument[0];
            current_PCB->disk_data->sector_id = SystemCallData->Argument[1];
            current_PCB->disk_data->disk_operation = DISK_READ;

            disk_read(SystemCallData->Argument[0], SystemCallData->Argument[1], SystemCallData->Argument[2]);

            memcpy(SystemCallData->Argument[2], current_PCB->disk_data->buffer, PGSIZE);
            memset(current_PCB->disk_data->buffer, '\0', PGSIZE);

            break;

        case SYSNUM_DISK_WRITE:
            // make sure the disk number is valid
            if (SystemCallData->Argument[0] < 1 || SystemCallData->Argument[0] > MAX_NUMBER_OF_DISKS) {
                printf("Error, call to an invalid disk number\n");
                break;
            }

            // make sure the sector is valid
            if (SystemCallData->Argument[1] < 0 || SystemCallData->Argument[1] > NUM_LOGICAL_SECTORS) {
                printf("Error, call to an invalid sector requested\n");
                break;
            }

            // allocate the disk queue
            if (disk_queue == NULL) {
                disk_queue = (PCB**) calloc(sizeof(PCB*), MAX_NUMBER_OF_DISKS);
            }

            // allocate the DISK data on the process
            if(current_PCB->disk_data == NULL)
                current_PCB->disk_data = calloc(sizeof(DISK*), 1);
            memset(current_PCB->disk_data->buffer, '\0', PGSIZE);

            // store the disk data on the PCB
            current_PCB->disk_data->disk_id = SystemCallData->Argument[0];
            MEM_WRITE(Z502DiskSetID, &(current_PCB->disk_data->disk_id));
            MEM_READ(Z502DiskStatus, &disk_status);

            //sleep till free
            while (!(disk_status == DEVICE_FREE)) {
                sleep_process(20, current_PCB);
                switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
                MEM_WRITE(Z502DiskSetID, &(current_PCB->disk_data->disk_id));
                MEM_READ(Z502DiskStatus, &disk_status);
            }

            // call the wrapper function for handling disk writing
            disk_write(SystemCallData->Argument[0], SystemCallData->Argument[1], SystemCallData->Argument[2]);
            break;
        case SYSNUM_DEFINE_SHARED_AREA:
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
        //printf("Function: %s address1 : (%ld, %ld) address2: (%ld, %ld)\n", argv[1], &func, func, &test1a, test1a);

        if (func != NULL) {
                //test_process = os_make_process(argv[1], DEFAULT_PRIORITY, &error_response, func, USER_MODE);
                test_process = os_make_process(argv[1], DEFAULT_PRIORITY, &error_response, func, KERNEL_MODE);
                switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
        }
        else {
            printf("Error!  Unrecognized entry point!\n");
        }
    }
}                                               // End of osInit

/**
* This function is responsible for creating all processes through the execution of the programs.
* The arguments are all part of the process struct and they tell a process what to do when it
* is created. It returns a fully functional process in whatever state is specified in the argument
* list. Example: if state == READY, the process would be added to the process list and the ready queue.
*/
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

    int i = 0;
    for (i = 0; i < MAX_MSG_COUNT; i++) {       // clear initial message buffer
        pcb->inbound_messages[i] = NULL;
    }

    pcb->delay = 0;                                 // start time = now (zero)
    pcb->pid = gen_pid;                             // assign pid
    gen_pid++;
    pcb->state=CREATE;
    pcb->time_spent_processing = 0;
    memset(pcb->pagetable, 0, VIRTUAL_MEM_PGS+1);   // assign pagetable

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
// Takes a PCB pointer and returns nothing after the PCB has been removed
void os_destroy_process(PCB* pcb) {
    if (current_PCB->pid != root_process_pcb->pid) { //only the root can destroy processes
        printf("error, only root can destroy processes\n");
        return;
    }

    //scheduler_printer("TERMINATED");
    remove_from_list(timer_queue, pcb->pid);

    Z502DestroyContext(&pcb->context);
    free(pcb);
}

/**
 *  This function is responsible for removing processes
 *  from the ready queue and switching to their context
 *  when they are available. It also adds processes
 *  to the timer queue when they are sleeping
 */
void dispatcher() {
    INT32 lock_result;
    int i = 0;
    while (TRUE) {

        i++;
        //if ((i % 100) == 0)
        //    printf("iterating round: %i\n", i);
//        if (i > 1000) {
//            printf("limiting to %i iterations before forced quit\n", i);
//            Z502Halt();
//        }

        // Check for terminated processes.
        Node *cursor = process_list;
        while (cursor != NULL) {
            if (cursor->data != NULL) {
                if (cursor->data->state == TERMINATE) {
                    PCB* dead_process = remove_from_list(process_list, cursor->data->pid);
                    //printf("dead process: %s %i\n", dead_process->name, dead_process->pid);
                    os_destroy_process(dead_process);
                    cursor = process_list;
                }
                else
                    cursor = cursor->next;
            }
            else
                cursor = cursor->next;
        }

        if (add_next_to_timer) {
            add_next_to_timer = FALSE;

            if (get_length(timer_queue) > 0) {
                INT32 current_time;
                MEM_READ(Z502ClockStatus, &current_time);
                INT32 sleep_time = timer_queue->data->delay - current_time;
                if (sleep_time < 0)
                    sleep_time = 0;
                MEM_WRITE(Z502TimerStart, &(sleep_time));
            }
        }

        if (get_length(process_list) == 0) {        //If no active processes then halt
            //printf("No processes exist other than root, halting\n");
            Z502Halt();
        }

        if (root_process_pcb->state == TERMINATE) {
            //printf("Root processed killed.  halting\n");
            Z502Halt();
        }

        LinkedList ready_queue;            // Holds all processes that are currently waiting to be run
        ready_queue = build_ready_queue(process_list);

        if(ready_queue->data != NULL) {
            PCB* process_to_run = ready_queue->data;
            process_to_run->state = RUNNING;
            free_ready_queue(ready_queue);

            switch_context(process_to_run, SWITCH_CONTEXT_SAVE_MODE);
        }
        else {
            free_ready_queue(ready_queue);
            CALL( Z502Idle() );
        }
    }
    printf("Error, I should never ever ever get here\n");
    Z502Halt();
}

/*********************************************************
 * Switches contexts for the current PCB
**********************************************************/
void switch_context( PCB* pcb, short context_mode) {
    INT32 current_time;
    MEM_READ(Z502ClockStatus, &current_time);

    if (current_PCB != NULL) {
        if (current_PCB->state == RUNNING) {
            current_PCB->time_spent_processing += (current_time - last_context_switch);
            current_PCB->state = READY;
        }
    }

	current_PCB = pcb;
    pcb->state = RUNNING;      //update the PCB state to RUN
    last_context_switch = current_time;

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

/**
* This function takes a process and a time to sleep and it
* puts the process in a sleeping state for "sleep_time"
* clock cycles. It adds the process to the timer_queue until it
* is time to wake up. It returns nothing when it completes.
*/
void sleep_process(INT32 sleep_time, PCB* sleeping_process) {
    INT32 current_time;
    MEM_READ(Z502ClockStatus, &current_time);
    INT32 wait_time = current_time + sleep_time;
    sleeping_process->delay = wait_time;
    add_to_list(timer_queue, sleeping_process);
    sleeping_process->state = SLEEPING;

    INT32 ticks_till_wake = timer_queue->data->delay - current_time;
    if (ticks_till_wake < 0)       //TODO I should validate this or throw an error if it is ever < 0
        ticks_till_wake = 0;

    MEM_WRITE(Z502TimerStart, &timer_queue->data->delay);
}

/**
* This function is just a cleaner way to handle the various
* function calls from the command line arguments. It makes the
* above functions a little easier to read.
*/
func_ptr get_function_handle(char *name) {
    //printf("looking for function handle with name: %s\n", name);
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
    else if ( strcmp( name, "test1g" ) == 0 )
        response = (void*) test1g;
    else if ( strcmp( name, "test1h" ) == 0 )
        response = (void*) test1h;
    else if ( strcmp( name, "test1i" ) == 0 )
        response = (void*) test1i;
    else if ( strcmp( name, "test1j" ) == 0 )
        response = (void*) test1j;
    else if ( strcmp( name, "test1k" ) == 0 )
        response = (void*) test1k;
    else if ( strcmp( name, "test1l" ) == 0 )
        response = (void*) test1l;
    else if ( strcmp( name, "test1m" ) == 0 )
        response = (void*) test1m;
    else if ( strcmp( name, "test2a" ) == 0 ) {
        response = (void*) test2a;
        print_memory = 1;
        print_schedule = 0;
    }
    else if ( strcmp( name, "test2b" ) == 0 ) {
        response = (void*) test2b;
        print_memory = 1;
        print_schedule = 0;
    }
    else if ( strcmp( name, "test2c" ) == 0 ) {
        response = (void*) test2c;
        print_schedule = 1;
        print_memory = 0;
    }
    else if ( strcmp( name, "test2d" ) == 0 ) {
        response = (void*) test2d;
        print_schedule = 1;
        print_memory = 0;
    }
    else if ( strcmp( name, "test2e" ) == 0 ) {
        response = (void*) test2e;
        print_memory = 1;
        print_schedule = 1;
    }
    else if ( strcmp( name, "test2f" ) == 0 ) {
        response = (void*) test2f;
        print_schedule = 0;
        print_memory = 1;
    }
    else if ( strcmp( name, "test2g" ) == 0 ) {
        response = (void*) test2g;
        print_memory = 0;
        print_schedule = 0;
    }
    else if ( strcmp( name, "test2h" ) == 0 )
        response = (void*) test2h;
    else if ( strcmp( name, "test2cAlt") == 0)
        response = (void*) test2cAlt;
    else
        response = NULL;
    return response;
}

/**
* Print out the state of the processes whenever it is called.
* This is basically a glorified wrapper for all these state_printer calls
*/
void scheduler_printer(char* action) {
    int time;
    PCB* p;

    // this makes sure we don't print out state information
    // all the time if it is not needed
    if(print_schedule == 1) {
        MEM_READ(Z502ClockStatus, &time);
        SP_setup(SP_TIME_MODE, time);

        SP_setup_action(SP_ACTION_MODE, action);

        if(current_PCB != NULL) {
            SP_setup(SP_TARGET_MODE, current_PCB->pid);
            SP_setup( SP_RUNNING_MODE, current_PCB->pid );
        }

        SP_print_header();
        SP_print_line();
    }
}

/**
* Prints out the current state of everything in memory
* both virtual and physical
*/
void memory_printer() {
    int i = 0;
    int state = 0;

    // makes sure we aren't printing out memory stuff if it is not needed
    if(print_memory == 1) {
        for(i = 0; i <= PHYS_MEM_PGS; i++) {
            if(frame_list[i].page_id >= 0 && frame_list[i].page_id < VIRTUAL_MEM_PGS) {
                state = ((Z502_PAGE_TBL_ADDR[frame_list[i].page_id] & PTBL_VALID_BIT) >> 13) +
                        ((Z502_PAGE_TBL_ADDR[frame_list[i].page_id] & PTBL_MODIFIED_BIT) >> 13) +
                        ((Z502_PAGE_TBL_ADDR[frame_list[i].page_id] & PTBL_REFERENCED_BIT) >> 13);

                MP_setup(frame_list[i].frame_id, current_PCB->pid, frame_list[i].page_id, state);
            }
        }

        MP_print_line();
        printf("\n");
    }
}

/**
* This function takes a process and a message list and adds the incoming
* messages to the corresponding process. This is used in test 1i - 1k.
* It returns true when the messages are successfully added to the process
* and false otherwise.
*/
BOOL enqueue_message(PCB* target_process, MESSAGE* inbound_message) {
    if (target_process == NULL)
        return FALSE;

    int i = 0;
    for (i = 0; i < MAX_MSG_COUNT; i++) {
        if (target_process->inbound_messages[i] == NULL) {
            target_process->inbound_messages[i] = inbound_message;
            return TRUE;
        }
    }
    return FALSE;
}

/**
 *   Removes a message from the pcb->inbound_messages[] array and returns it to the requestor
 */
MESSAGE* remove_message(PCB* pcb, int index) {
    if ((index < 0) || (index >= MAX_MSG_COUNT ))
        return NULL;

    if (pcb->inbound_messages[index] == NULL)
        return NULL;

    MESSAGE* retval = pcb->inbound_messages[index];
    pcb->inbound_messages[index] = NULL;
    return retval;
}

/**
 *  Returns the index array from the pcb->inbound_messages[] array that corresponds to the request
 *  set source_pid = -1 to grab the first available message
 *  If no messages exist then it returns -1;
 */
int find_message_by_source(PCB* pcb, int source_pid) {
    int i;
    for (i = 0; i < MAX_MSG_COUNT; i++) {
        if (pcb->inbound_messages[i] != NULL) {
            if (pcb->inbound_messages[i]->source_pid == source_pid || source_pid == -1)
                return i;
        }
    }
    return -1;
}


/**
 *  This function reads through a list of messages attached to a PCB and returns the index of a handled broadcast message
 */
int find_handled_message(PCB* pcb) {
    int i;
    for (i = 0; i < MAX_MSG_COUNT; i++) {
        if (pcb->inbound_messages[i] != NULL) {
            if (pcb->inbound_messages[i]->handled)
                return i;
        }
    }
    return -1;
}

/**
 *  This method is called after a broadcast message has been handled by a recipient.  This function makes sure the message
 *  cannot be handled by any other recipient.
 */
void clear_handled_broadcast_message() {
    Node *cursor = process_list;
    MESSAGE* to_be_destroyed = NULL; // a handle to the one message that needs to be destroyed
    int index;

    while (cursor != NULL) {
        if (cursor->data != NULL) {
            index = find_handled_message(cursor->data);

            while (index != -1) {
                to_be_destroyed = remove_message(cursor->data, index); // doesn't really matter, these should all be the same message

                index = find_handled_message(cursor->data);
            }
        }

        cursor = cursor->next;
    }
}

/**********************************************************
* All Memory management stuff is defined below here
***********************************************************/
UINT16 find_empty_frame(INT32 status) {
    int i;

    // run through all the Physical memory and find an available frame
    for(i = 0; i < PHYS_MEM_PGS; i++) {
        if((frame_list[i]).in_use == FALSE) {
            frame_list[i].in_use = TRUE;
            frame_list[i].pid = current_PCB->pid;
            frame_list[i].page_id = status;
            return i;
        }
    }

    // if no frames are found by now, return -1
    return -1;
}

/**
* no empty frames so find an unused one and release it
*/
UINT16 page_replacement() {
    long disk_id;
    long sector_id;
    long frame_id;
    long page_id;
    int  victim = current_PCB->pid + 1;

    page_id = frame_list[victim].page_id;
    frame_id = frame_list[victim].frame_id;

    disk_id = current_PCB->pid + 1;
    sector_id = page_id;

    Z502_PAGE_TBL_ADDR[page_id] = PHYS_MEM_PGS;
    Z502_PAGE_TBL_ADDR[page_id] = (UINT16)Z502_PAGE_TBL_ADDR[page_id] & PTBL_PHYS_PG_NO;

    // copy the old info into the shadow table
    shadow_table[page_id].disk_id = disk_id;
    shadow_table[page_id].sector_id = sector_id;
    shadow_table[page_id].frame_id = frame_id;
    shadow_table[page_id].page_id = page_id;
    shadow_table[page_id].in_use = TRUE;

    // write the contents of the buffer to the disk
    if(current_PCB->disk_data->buffer != NULL)
        disk_write(disk_id, sector_id, current_PCB->disk_data->buffer);

    // free up the frame
    frame_list[victim].in_use = FALSE;

    return victim;
}

// Just a wrapper for reading the disk status
// because it has to be done frequently
int get_disk_status(long disk_id) {
    INT32 disk_status;
    MEM_WRITE(Z502DiskSetID, &disk_id);
    MEM_READ(Z502DiskStatus, &disk_status);
    return disk_status;
}

/**
* This function is responsible for reading data from the disk.
*/
void disk_read(long disk_id, long sector_id, char* read_buffer) {
    INT32               disk_status;
    INT32               disk_action;
    INT32               disk_start;

    MEM_WRITE(Z502DiskSetID, &(current_PCB->disk_data->disk_id));
    MEM_READ(Z502DiskStatus, &disk_status);

    while (!(disk_status == DEVICE_FREE)) { //sleep till free
        sleep_process(20, current_PCB);
        switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
        MEM_WRITE(Z502DiskSetID, &(current_PCB->disk_data->disk_id));
        MEM_READ(Z502DiskStatus, &disk_status);
    }

    MEM_WRITE(Z502DiskSetID, &(current_PCB->disk_data->disk_id));
    MEM_WRITE(Z502DiskSetSector, &(current_PCB->disk_data->sector_id));

    MEM_WRITE(Z502DiskSetBuffer, (INT16 *)(current_PCB->disk_data->buffer));

    disk_action = 0;
    MEM_WRITE(Z502DiskSetAction, &(disk_action));  //set to read from disk
    disk_start = 0;
    disk_queue[(INT32)current_PCB->disk_data->disk_id] = current_PCB;
    MEM_WRITE(Z502DiskStart, &(disk_start));

    MEM_WRITE(Z502DiskSetID, &(current_PCB->disk_data->disk_id));
    MEM_READ(Z502DiskStatus, &disk_status);

    if(disk_status == DEVICE_FREE) {
        printf("Error!  Disk is free and should be in use\n");
    }
    else if(disk_status == ERR_BAD_DEVICE_ID) {
        printf("Error!  Disk write was not set up correctly\n");
    }
    else {
        // suspend the process for the duration of the read
        current_PCB->state = SUSPEND;
        current_PCB->suspend_reason = WAITING_FOR_DISK;

        // switch to the root process so we keep running while the other process is reading
        switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
    }
}

/**
* This function is responsible for writing data to the disk.
*/
void disk_write(long disk_id, long sector_id, char* write_buffer) {
    INT32 disk_status;
    MEM_WRITE(Z502DiskSetID, &disk_id);
    MEM_READ(Z502DiskStatus, &disk_status);

    // make sure the device is available
    if (disk_status == DEVICE_FREE) {
        MEM_WRITE(Z502DiskSetID, &disk_id);
        MEM_WRITE(Z502DiskSetSector, &sector_id);
        MEM_WRITE(Z502DiskSetBuffer, (INT32*) write_buffer);

        // tell the disk we are going to write
        disk_status = 1;
        MEM_WRITE(Z502DiskSetAction, &disk_status);
        disk_status = 0;

        // write what we need to write and add the process to the disk queue
        MEM_WRITE(Z502DiskStart, &disk_status);
        MEM_WRITE(Z502DiskSetID, &disk_id);
        MEM_READ(Z502DiskStatus, &disk_status);
        disk_queue[(INT32)current_PCB->disk_data->disk_id] = current_PCB;

        // suspend the process until we can finish writing
        current_PCB->state = SUSPEND;
        current_PCB->suspend_reason = WAITING_FOR_DISK;
        switch_context(root_process_pcb, SWITCH_CONTEXT_SAVE_MODE);
    }
    else if(disk_status == DEVICE_IN_USE) {
        printf("Error, disk should not already be in use!\n");
    }
    else {
        printf("Catch All from disk_write! Should never get here!\n");
    }
}