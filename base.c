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

#include	     	 "my_globals.h"

// These loacations are global and define information about the page table
extern UINT16        *Z502_PAGE_TBL_ADDR;
extern INT16         Z502_PAGE_TBL_LENGTH;

extern void          *TO_VECTOR [];

char                 *call_names[] = { "mem_read ", "mem_write",
                            "read_mod ", "get_time ", "sleep    ",
                            "get_pid  ", "create   ", "term_proc",
                            "suspend  ", "resume   ", "ch_prior ",
                            "send     ", "receive  ", "disk_read",
                            "disk_wrt ", "def_sh_ar" };

int 	             gen_pid = 0;            // used for generating a process id
PCB_str	             *current_PCB = NULL;    // this is running PCB


/************************************************************************
    INTERRUPT_HANDLER
        When the Z502 gets a hardware interrupt, it transfers control to
        this routine in the OS.
************************************************************************/
void    interrupt_handler( void ) {
    INT32              device_id;
    INT32              status;
    INT32              Index = 0;
    static BOOL        remove_this_in_your_code = TRUE;   /** TEMP **/
    static INT32       how_many_interrupt_entries = 0;    /** TEMP **/

    // Get cause of interrupt
    MEM_READ(Z502InterruptDevice, &device_id );
    // Set this device as target of our query
    MEM_WRITE(Z502InterruptDevice, &device_id );
    // Now read the status of this device
    MEM_READ(Z502InterruptStatus, &status );

    /** REMOVE THE NEXT SIX LINES **/
    how_many_interrupt_entries++;                         /** TEMP **/
    if ( remove_this_in_your_code && ( how_many_interrupt_entries < 20 ) )
        {
        printf( "Interrupt_handler: Found device ID %d with status %d\n",
                        device_id, status );
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
	INT32				Time;
    short               i;

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

    switch (call_type) {
		//get the time system call
        case SYSNUM_GET_TIME_OF_DAY:
			MEM_READ ( Z502ClockStatus, &Time );
			*(INT32*)SystemCallData->Argument[0] = Time;
			break;
		//terminate system call
		case SYSNUM_TERMINATE_PROCESS:
			Z502Halt();
			break;
        case SYSNUM_SLEEP:
            break;
		default:
			printf( "ERROR! call_type not recognized!\n" );
			printf( "Call_type is %i\n", call_type );
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
        CALL ( Z502MakeContext( &next_context, (void *)sample_code, KERNEL_MODE ) );
		printf("1. HEREEEEEE");
        CALL ( Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &next_context ) );
		printf("2. HEREEEEEE");
    }                   /* This routine should never return!!           */
		/*  This should be done by a "os_make_process" routine, so that
        test0 runs on a process recognized by the operating system.    */
	else if(( argc > 1 ) && ( strcmp( argv[1], "test0" ) == 0 ) ) {
		Z502MakeContext( &next_context, (void *)test0, USER_MODE );
		Z502SwitchContext( SWITCH_CONTEXT_KILL_MODE, &next_context );
		CALL ( os_create_process(&i, "test0", test0, &i) );
	}
}                                               // End of osInit

/************************************************************************
    os_create_process:

    This function creates processes. When it creates a parent process,
    it will directly make it run. Child processes will not run, but instead be 
    put into the ready queue.

    Parameters:
        INT32* pid:        the unique process ID
        char*  name:       the unique name of the process
        void*  prog_addr:  the pointer used for creating contexts
        INT32* error:      address of return error
*************************************************************************/
INT32 os_create_process(INT32* pid, char* name, void* prog_addr, INT32* error) {
	PCB_str *PCB = (PCB_str *)(malloc(sizeof(PCB_str)));    //allocate memory for PCB
       
	PCB->p_time = 0;                                        //now is zero
	PCB->p_id = gen_pid;                                    //assign pid
    gen_pid++;
	PCB->p_state=CREATE;

	memset(PCB->p_name, 0, MAX_NAME+1);                    //assign process name
	strcpy(PCB->p_name,name);                              //enter name 
    PCB->disk_io.flag=0;                                   //assign disk read flag
    memset(PCB->pagetable, 0, VIRTUAL_MEM_PGS+1);          //pagetable 

	if (current_PCB != NULL) {
        PCB->p_parent = current_PCB->p_id;                //assign parent id
	}
    else {
        PCB->p_parent = -1;                               //-1 means this process is the parent process
	}

	(*error) = ERR_SUCCESS;                           //return error value
	(*pid) = PCB->p_id;                               //return pid

	return 0; 
}

/************************************************************************
	context operations:

	There are two functions: make_context and switch_context for a PCB.
	switch_context makes this PCB run on the current_PCB

	These functions use Z502MAKECONTEXT and Z502SWITCHCONTEXT

*************************************************************************/
void make_context ( PCB_str * PCB, void *procPTR ){
	ZCALL( Z502MakeContect( &PCB->context, procPTR, USER_MODE ));
}

void switch_context ( PCB_str * PCB ){
	current_PCB = PCB;
    current_PCB -> p_state =RUN;      //update the PCB state to RUN
	ZCALL( Z502SwitchContext( SWITCH_CONTEXT_SAVE_MODE, &current_PCB->context ));
}
