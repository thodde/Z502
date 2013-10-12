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
#define			MAX_PIDs	       10
#define			MAX_NAME	       16
#define			MAX_MSG			   64
#define			MAX_MSG_COUNT	   10

// Flags to determine whether or not to run a process upon creation
#define         NOTRUN              0
#define         RUN                 1

typedef struct {
    INT32   pid;
    int     delay;
    char    name[MAX_NAME];
    INT32   parent;
    INT32   state;
    INT32   mode;
    INT32   priority;
    void*   context;
} PCB;

//******** Function Prototypes *********//
PCB* os_make_process(char* name, INT32* error);

void switch_context( PCB* PCB );

INT32 add_to_timer_queue(PCB* entry);

#endif