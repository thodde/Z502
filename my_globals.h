#include    "stdio.h"
#include	"stdlib.h"

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

//PCB structure
typedef struct {
	char		p_name[MAX_NAME+1];      //name
	INT32	    p_id;                    //process id
    INT32		p_time;                  //wake up time
	INT32		p_parent;                // parent control
    INT32       p_state;                 //state
	void	    *context;
	void		*next;                    //link to next node
	void        *sentBox;                 //to sent queue
    void        *recvBox;                 // for recieve
    INT32		msg_count;
      
    INT16		pagetable[VIRTUAL_MEM_PGS]; //page table
}PCB_str;

//******** Function Prototypes *********//
INT32 os_make_process(INT32* pid, char* name, INT32* error);

void make_context( PCB_str* PCB, void* procPTR);
void switch_context( PCB_str* PCB );

INT32 add_to_timer_queue(PCB_str * entry);
