#include        "stdio.h"
#include	"stdlib.h"

///PCB State Information
#define CREATE              100
#define	READY               101
#define	RUNNING             102
#define SUSPEND             103
#define	SLEEPING            104
#define SWAPPED             105
#define	TERMINATE	        106

//PCB information
#define	MAX_PIDs            10
#define	MAX_NAME            16
#define	MAX_MSG             64
#define	MAX_MSG_COUNT       10

//about disk define
#define DISK_IO_BUF_SIZE    16

//disk io structure                           
typedef struct {
        INT32   disk_id;
        INT32   sector;
        char    buf[DISK_IO_BUF_SIZE];
        char    buf2[DISK_IO_BUF_SIZE];
        INT32   op;
        INT32   flag;
} DISK_IO_str;

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
    INT16		 pagetable[VIRTUAL_MEM_PGS]; //page table
    DISK_IO_str  disk_io;                    //disk use information
} PCB_str;

//******** Function Prototypes *********//
INT32 os_make_process(INT32* pid, char* name, void* prog_addr, INT32* error);
