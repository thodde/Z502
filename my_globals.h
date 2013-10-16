#ifndef MY_GLOBALS
#define MY_GLOBALS

#include    "stdio.h"
#include	"stdlib.h"
#include    "global.h"

///PCB State Information
#define	        CREATE		       100
#define			READY			   101
#define			RUNNING			   102
#define         SUSPEND            103
#define			SLEEPING		   104
#define         SWAPPED            105
#define			TERMINATE		   106

// PCB information
#define			MAX_PROCESSES      10
#define			MAX_NAME	       16
#define			MAX_MSG			   64
#define			MAX_MSG_COUNT	   10

// PROCESS PRIORITY LIMITS
#define         MIN_PRIORITY        0
#define         DEFAULT_PRIORITY    50
#define         MAX_PRIORITY        100

typedef struct {
    INT32   pid;
    INT32   delay;
    char    name[MAX_NAME];
    INT32   parent;
    INT32   state;
    INT32   mode;
    INT32   priority;
    void*   context;
    long    time_spent_processing;
} PCB;

typedef void* func_ptr;

//******** Function Prototypes *********//
PCB* os_make_process(char* name, INT32 priority, INT32* error, void* entry_point, INT32 mode);
void os_destroy_process(PCB* pcb);
void switch_context( PCB* pcb, short context_mode);
void pcb_cascade_delete_by_parent(INT32 parent_pid);
void dispatcher(void);
void sleep_process(INT32 sleep_time, PCB* sleeping_process);
//void get_function_handle(char *name, void** ptr);
func_ptr get_function_handle(char *name);

#endif