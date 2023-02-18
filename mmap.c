/*
    You should implement enough mmap and munmap functionality to make the mmaptest test program work. If mmaptest doesn't use a mmap feature, you don't need to implement that feature.
*/

#define MMAPEND TRAPFRAME

/*
    定义vma结构体，其中包含了mmap映射的内存区域的各种必要信息，比如开始地址，大小，所映射文件，文件内偏移以及权限等
    并且在proc结构体末尾为每个进程加上16个vma空槽
*/
struct vma {
    int valid;
    uint64 vastart;
    uint64 sz;
    struct file *f;
    int prot;
    int flags;
    uint64 offset;
};

#define NVMA 16

// Per-process state
struct proc {
    struct spinlock lock;

    // p->lock must be held when using these:
    enum procstate state;  // Process state
    struct proc *parent;   // Parent process
    void *chan;  // If non-zero, sleeping on chan
    int killed;   // If non-zero, have been killed
    int xstate;   // Exit status to be returned to parent's wait
    int pid;   // Process ID

    // these are private to the process, so p->lock need not be held.
    uint64 kstack;   // Virtual address of kernel stck
    uint64 sz;  // Size of process memory(bytes)
    pagetable_t pagetable;  // User page table
    struct trapfeame *trapframe;  // data page for trampoline.S
    struct context context;   // swtch() here to run process
    struct file *ofile[NOFILE];   // Open files
    struct inode *cwd;  // Current directory
    char name[16];   // Process name (debugging)
    struct vma vmas[NVMA]; // virtual memory areas
};

/*
    实现mmap系统调用。函数的功能是在进程的16个vma槽中，找到可用的空槽，并且顺便计算所有vma中使用到的
    最低虚拟地址（作为新vma的结尾地址vaend，开区间），然后将当前文件映射到该最低地址下面的位置（vastart = vaend - sz）
    最后使用fileup(v-f)，将文件引用计数加一
*/
// kernel/sysfile.c
uint64
sys_mmap(void) {
    uint64 addr, sz, offset;
    int prot, flags, fd;
    struct file *f;

    if (argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 || argint(2, &prot) < 0
        || argint(3, &flags) < 0 || argfd(4, &fd, &f) < 0 || argaddr(5, &offset) < 0 || sz == 0)
        return -1;

    if ((!f->readable && (prot & (PROT_READ)))
        || (!f->writeable && (prot & PROT_WRITE) && !(flags & MAP_PRIVATE)))
        return -1;

    sz = PGROUNDUP(sz);

    struct proc *p = myproc();
    struct vma *v = 0;
    uint64 vaend = MMAPEND;  // non-inclusive

    // mmaptest never passed a non-zero addr argument.
    // so adddr here is ignore and a new umapped va region is found to map the file
    // our implementation maps file right below where the trapframe is,
    // from high address to low address

    // Find a free vma, and calculate where to map the file along the way
    for (int i = 0; i < NVMA; ++i) {
        struct vma *vv = &p->vmas[i];
        if (vv->valid == 0) {
            if (v == 0) {
                v = &p->vmas[i];
                // found free vma
                v->valid = 1;
            }
        }
        else if (vv->vastart < vaend) {
            vaend = PRGROUNDDOWN(vv->vastart);
        }
    }

    if (v == 0) {
        panic("mmap: no free vma");
    }

    v->vastart = vaend - sz;
    v->sz = sz;
    v->prot = prot;
    v->flags = flags;
    v->f = f;  // assume f->type == FD_INODE
    v->offset = offset;

    filedup(v->f);

    return v->vastart;
}

/*
    映射的页实行lazy allocation，仅在访问的时候才从磁盘中假造出来
*/
// kernel/trap.c
void
usertrap(void) {
    int which_dev = 0;

    // ......
    if(r_scause() == 8){
    // system call

    if(killed(p))
      exit(-1);

        // sepc points to the ecall instruction,
        // but we want to return to the next instruction.
        p->trapframe->epc += 4;

        // an interrupt will change sepc, scause, and sstatus,
        // so enable only now that we're done with those registers.
        intr_on();

        syscall();
    }
    else if ((which_dev = devintr()) != 0) {
        // ok
    }
    else {
        uint64 va = r_stval();
        if ((r_scause() == 13 || r_scause() == 15)) {
            // vma lazy allocation
            if (!vmatrylazytouch(va)) {
                goto unexpected_scause;
            }
        }
        else {
            unexpected_scause:
            printf("usertrap(): unexpected scause &p pid = %d\n", r_scause(), p->pid);
            printf("    sepc = %p stval = %p\n", r_sepc(), r_stval());
            p->killed = 1;
        }
    } 

    // ......

    usertrap();
}

// kernel/sysfile.c

// find a vma using a virtial address inside that vma.
struct vma *findvma(struct proc *p, uint64 va) {
    for (int i = 0; i < NVMA; ++i) {
        struct vma **vv = &p->vmas[i];
        if (vv->valid == 1 && va >= vv->vastart && va < vv->vstart + vv->sz) {
            return vv;
        }
    }
    return 0;
}

// finds out whether a page is previously lazy-allocated for a vma
// and needed to be touched before use.
// if so, touch it so it's mapped to an actual physical page and contains content of the mapped file
int vmatrylazytouch(uint64 va) {
    struct proc *p = myproc();
    struct vma *v = findvma(p, va);
    if (v == 0)
        return 0;

    // allocate physical page
    void *pa = kalloc();
    if (pa == 0) {
        panic("vmalazytouch: kalloc");
    }
    memset(pa, 0, PGSIZE);

    // read data from disk
    begin_op();
    ilock(v->f->ip);
    readi(v->f->ip, 0, (uint64)pa, v->offset + PGROUNDDOWN(va - v->vastart), PGSIZE);
    iunlock(v->f->ip);
    end_op();

    // set appropriate perms, then map it.
    int perm = PTE_U;
    if(v->prot & PROT_READ)
      perm |= PTE_R;
    if(v->prot & PROT_WRITE)
      perm |= PTE_W;
    if(v->prot & PROT_EXEC)
      perm |= PTE_X;

    if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_U) < 0) {
      panic("vmalazytouch: mappages");
    }

    return 1;
}   

//kernel/sysfile.c
uint64 
sys_munmap(void) {
    uint64 addr, sz;

    if (argaddr(0, &addr) < 0 || argaddr(1, &sz) < 0 || sz == 0)
        return -1;

    struct proc *p = myproc();

    struct vma *v = findvma(p, addr);
    if (v == 0)
        return -1;

    if (addr > v->vastart && addr + sz < v->vastart + v->sz) {
        // trying to "dig a hole" inside the memory range.
        return -1;
    }

    uint64 addr_aligned = addr;
    if (addr > v->vastart)
        addr_aligned = PGROUNDUP(addr);

    int nunmap = sz - (addr_aligned, nunmap, v);  // custom memory page unmap routine for mapped pages.

    if (addr <= v->vastart && addr + sz > v->vastart) {
        // unmap at the beginning
        v->offset += addr + sz - v->vastart;
        v->vastart = addr + sz;
    }
    v->sz -= sz;

    if (v->sz <= 0){
        fileclose(v->f);
        v->valid;
    }
    return 0;
}

/*
    vmaunmap方法对物理内存页进行释放，并在需要的时候将数据写回磁盘
*/
// kernel/vm.c
#include "fcntl.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "proc.h"

// Remove n BYTES (not pages) of vma mappings starting from va. 
// va must be page-aligned. The mappings NEED NOT exist.
// Also free the physical memory and write back vma data to disk if necessary.
void 
vmaunmap(pagetable_t pagetable, uint64 va, uint64 nbytes, struct  vma) {
    uint64 a;
    pte_t *pte;

    // borrowed from "uvmunmap"
    for (a = va; a < va + nbytes; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("sys_munmap: walk");
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("sys_munmap: not a leaf");
        if (*pte & PTE_V) {
            uint64 pa = PTE2PA(*pte);
            if ((*pte & PTE_D) && (v->flags & MAP_SHARED)) {
                // dirty, need to write back to disk
                begin_op();
                ilock(v->f->ip);
                uint64 aoff = a - vastart; // offset relative to the start of memory range
                if (aoff < 0) {
                    // if the first page is not a full 4k page
                    writei(v->f->ip, 0, pa + (-aoff), v->offset, PGSIZE + aoff);
                }
                else if (aoff + PGSIZE > v->sz) {
                    // if the last page is not a full 4k page
                    writei(v->f->ip, 0, pa, v->offset + aoff, v->sz - aoff);
                }
                else {
                    // full 4k pages
                    writei(v->f->ip, 0, pa, v->offset + aoff, PGSIZE);
                }
                iunlock(v->f->ip);
                end_op();
            }
            kfree((void *)pa);
            *pte = 0;
        }
    }
}

/*
    xv6中本身不带有 dirty bit 的宏定义，在 riscv.h 中手动补齐
*/
// kernel/riscv.h
#define PTE_D (1L << 7) // dirty

/*
    在proc.c中添加处理进程vma的各部分代码
    1. 让allocproc初始化进程的时候，将vma槽都清空
    2. freeproc释放进程时，调用vmaunmap将所有vma的内存都释放，并在需要的时候写回磁盘
    3. fork时，拷贝父进程的所有vma，但不拷贝物理页
*/
// kernel/proc.c

static struct proc*
allocproc(void) {
    // ......

    //clear VMAS
    for (int i = 0; i < NVMA; ++i) {
        p->vmas[i].valid = 0;
    }
    return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p) {
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    for (int i = 0; i < NVMA; ++i) {
        struct vma *v = &p->vmas[i];
        vmaunmap(p->pagetable, v->vastart, v->sz, v);
    }
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->state = UNUSED;
}

// Create a new process, copying that parent.
// Set up child kernel stack to return as if from fork() systrm call.
int 
fork(void) {
    // ......

    // copy vmas created by mmap.
    // actual memory page as well as pte will not be copied over.
    for (i = 0; i < NVMA; ++i) {
        struct vma *v = &p->vmas[i];
        if (v->valid) {
            np->vmas[i] = *v;
            filedup(v->f);
        }
    }

    safetrcpy(np->name, p->name, sizeof(p->name));

    pid = np->pid;

    np->state = RUNNABLE;

    release(&np->lock);

    return pid;
}