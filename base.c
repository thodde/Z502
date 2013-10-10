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


extern INT16 Z502_MODE;

// These locations are global and define information about the page table
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;

extern void          *TO_VECTOR [];

//TODO destroy this later or make it a better implementation
void * base_process;

// for keeping track of the current pid
int gen_pid = 0;
PCB_str	               *current_PCB = NULL;    // this is the currently running PCB

PCB_str		           *timerList = NULL;      //first node in the timer queue
PCB_str                *timer_tail = NULL;     //timer queue tail
int	                   total_timer_pid = 0;    //counter for the number of PCBs in the timer queue

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

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

    switch(device_id) {
        case(TIMER_INTERRUPT):
            MEM_READ(Z502ClockStatus, &Time);
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
    INT32               sleep_time;

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

    if (Z502_MODE == KERNEL_MODE) {
        printf("I am in kernel mode\n");
        if (strncmp(call_names[call_type], "get_time", 8) == 0) { // handles GET_TIME_OF_DAY
            //printf("This is the data I received: %li\n", *(SystemCallData->Argument[0]));
            MEM_READ(Z502ClockStatus, SystemCallData->Argument[0]);
        }
        //TODO This needs to be figured out why everything is being executed in KERNEL_MODE
        else if (strncmp(call_names[call_type], "term_proc", 9) == 0) {  // handles TERMINATE_PROCESS
            //TODO validate parameters
            //Z502DestroyContext(base_process);  I think this is used when killing any other process other than the base process

            //TODO this should only be called when the parent process is killed, not when any process is killed
            Z502Halt();

            //TERMINATE_PROCESS(SystemCallData->Argument[0], SystemCallData->Argument[1]);
            //printf("Returned with error: %i\n", SystemCallData->Argument[1]);
        }
        else if (strncmp(call_names[call_type], "sleep", 5) == 0) {
            MEM_READ( Z502TimerStatus, &current_time);
            sleep_time = SystemCallData->Argument[0];
            //current_PCB->p_time = (current_time+sleep_time);
            //add_to_timer_queue(current_PCB);
            Z502Idle();
        }
    }
    else if (Z502_MODE == USER_MODE) {
        printf("I am in user mode\n");
        if (strncmp(call_names[call_type], "get_time", 8) == 0) { // handles GET_TIME_OF_DAY
            //TODO validate parameters
            if ((SystemCallData->NumberOfArguments - 1) < 1) {
                //TODO throw error
            }
            else {
            // Need to validate that the the supplied memory block resides in context space
                GET_TIME_OF_DAY(SystemCallData->Argument[0]);
            }
        }
        else if (strncmp(call_names[call_type], "term_proc", 9) == 0) {  // handles TERMINATE_PROCESS
            //TODO validate parameters
            TERMINATE_PROCESS(SystemCallData->Argument[0], SystemCallData->Argument[1]);
            printf("Returned with error: %i\n", SystemCallData->Argument[1]);
        }
        else if (strncmp(call_names[call_type], "sleep", 5) == 0) {
            MEM_READ( Z502TimerStatus, &current_time);
            sleep_time = SystemCallData->Argument[0];
            //current_PCB->p_time = (current_time+sleep_time);
            //add_to_timer_queue(current_PCB);
            Z502Idle();
        }
    }
    else {
        printf("Error!  Current mode is unrecognized!!!\n");
    }
}                                               // End of svc

/************************************************************************
    osInit
        This is the first routine called after the simulation begins.  This
        is equivalent to boot code.  All the initial OS components can be
        defined and initialized here.
************************************************************************/

void    osInit( int argc, char *argv[]  ) {
    void                *next_context;
    INT32               i;

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
        Z502MakeContext( &next_context, (void *)sample_code, KERNEL_MODE );
        Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &next_context );
    }                   /* This routine should never return!!           */
    else if (( argc > 1 ) && ( strcmp( argv[1], "test0" ) == 0 ) ) {
        /*  This should be done by a "os_make_process" routine, so that
        test0 runs on a process recognized by the operating system.    */
        Z502MakeContext( &base_process, (void *)test0, USER_MODE );
        Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &base_process );
    }
    else if (( argc > 1 ) && ( strcmp( argv[1], "test1a" ) == 0 ) ) {
        /*  This should be done by a "os_make_process" routine, so that
        test1a runs on a process recognized by the operating system.    */
        //os_make_process( &i, "test1a", &i);
        Z502MakeContext( &base_process, (void *)test1a, USER_MODE );
        Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &base_process );
    }
}                                               // End of osInit

INT32 os_make_process(INT32* pid, char* name, INT32* error) {
    PCB_str *PCB = (PCB_str *)(malloc(sizeof(PCB_str)));    // allocate memory for PCB

    PCB->p_time = 0;                                        // start time = now (zero)
    PCB->p_id = gen_pid;                                    // assign pid
    gen_pid++;
    PCB->p_state=CREATE;

    memset(PCB->p_name, 0, MAX_NAME+1);                    // assign process name
    strcpy(PCB->p_name, name);                             // assign process name

    if (current_PCB != NULL)
        PCB->p_parent = current_PCB->p_id;                // assign parent id
    else
        PCB->p_parent = -1;                               // -1 means this process is the parent process

    (*error) = ERR_SUCCESS;                           // return error value
    (*pid) = PCB->p_id;                               // return pid

    make_context(PCB, base_process);
    //TODO: add PCB to ready queue here

    return 0;
}

/*********************************************************
 * Creates a context for the current PCB
**********************************************************/
void make_context( PCB_str* PCB, void* procPTR ) {
	Z502MakeContext( &PCB->context, procPTR, USER_MODE );
}

/*********************************************************
 * Switches contexts for the current PCB
**********************************************************/
void switch_context( PCB_str* PCB ) {
	current_PCB = PCB;
    current_PCB -> p_state = RUN;      //update the PCB state to RUN
	Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &current_PCB->context );
}

/************************************************************************
   add to timer queue:
        sort nodes based on wake up time (p_time in PCB);
        inputs are all PCB_str * entry
*************************************************************************/
INT32 add_to_timer_queue(PCB_str * entry) {
    PCB_str** ptrFirst= &timerList;                        // pointer to the timer queue head
    PCB_str*  PCB = (PCB_str*) (malloc(sizeof(PCB_str)));  // create memory for PCB
    memcpy(PCB, entry, sizeof(PCB_str));                   // copy new PCB to this memory space
    PCB_str* current = NULL;
    PCB_str* previous = NULL;

    entry->p_state = SLEEPING;                              //update state to sleep

    int flag = 0;

	// First one in the queue
	if ( *ptrFirst  == NULL) {
	    (*ptrFirst) = entry;
		timer_tail = entry;
        total_timer_pid++;
	}
	else {      // NOT the first PCB in the queue
        current = (*ptrFirst);

        while(current != NULL) {
            if(entry->p_time < current->p_time) {
                if(current == (*ptrFirst)) {
                    (*ptrFirst) = entry;
                    entry->next = current;
                    total_timer_pid++;
                    flag = 1;
                    break;
                }
                else {
                    previous->next = entry;
                    entry->next = current;
                    total_timer_pid++;
                    flag = 1;
                    break;
                }
            }
            else {
                previous = current;
                current = current->next;
            }
        }

        if(flag == 0) {
            timer_tail->next = entry;
            timer_tail = entry;
            total_timer_pid++;
        }
    }
	return -1;
}
