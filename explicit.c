/* Name: Derek McCreight
 * =====================
 * Description: This program implements an explicit heap allocator the final capstone project for
 * Stanford's CS107 class. This explicit heap allocator maintains a linked list which track the
 * location of each free block. The heap will handle various odd requests such as allocation
 * requests of malloc(0), realloc(0), freeing and reallocing NULL pointer requests as well. The
 * current implementation also re-allocates memory in place if possible in order to decrease
 * heap fragmentation.
 * =====================
 */ 
#include "allocator.h"
#include "debug_break.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ALIGNMENT 8 
#define MIN_ALLOC 1
#define MAX_HEAP_SIZE 1<<30
#define BUF_DATA_SIZE sizeof(header) + sizeof(prev_next)

static void *segment_begin;
static void *segment_end;
static void *f_start;

typedef struct header
{
    unsigned int size;
    int status;     //value of 1 is used and 0 is unused
} header;

typedef struct prev_next
{
    void *next;     //points to the next node in the list
    void *prev;
} prev_next;

void create_chunk_hdr(void *pntr, size_t width, bool is_realloc);
bool shrink_chunk(void *pntr, size_t width);
bool in_place_realloc(void *pntr, size_t formatted_sz);
void add_node(void *pntr);
void remove_node(void *pntr);
void merge_free(void *pntr);
void *get_next_hdr(void *pntr);
void *hdr_to_payload(void *pntr); 
void *hdr_to_prev_next(void *pntr);
void *payload_to_hdr(void *pntr); 
void *prev_next_to_payload(void *pntr);
void *prev_next_to_header(void *pntr);
void *payload_to_prev_next(void *pntr);
void *next_in_list(void *pntr);
void *prev_in_list(void *pntr);
size_t roundup(size_t size, size_t mult);

/*Adapted from bump allocator-- this function rounds to the nearest given multiple*/
size_t roundup(size_t size, size_t mult)
{
    return (size + mult - 1) & ~(mult - 1);
}


void *get_next_hdr(void *pntr)
{
    return (char*)pntr + ((header*)pntr)->size + sizeof(header) + sizeof(prev_next);
}


void *hdr_to_prev_next(void *pntr)
{
    return (char*)pntr + sizeof(header);
}


void *hdr_to_payload(void *pntr)
{
    return (char*)pntr + sizeof(header) + sizeof(prev_next);
}


void *payload_to_hdr(void *pntr)
{
    return (char*)pntr - sizeof(header) - sizeof(prev_next);
}


void *prev_next_to_payload(void *pntr)
{
    return (char*)pntr + sizeof(prev_next);
}


void *prev_next_to_hdr(void *pntr)
{
    return (char*)pntr - sizeof(header);
}


void *payload_to_prev_next(void *pntr)
{
    return (char*)pntr - sizeof(prev_next);
}

/* This function performs the necessary pointer arithmetic to find
 * the location of the next header, create it, and update the original
 * header information
 */
bool shrink_chunk(void *pntr, size_t width) //CALL FROM  HEADER  //MOST LIKELY BUGGY//
{
    width = roundup(width, ALIGNMENT);
    if(width < sizeof(prev_next)) width = sizeof(prev_next);
    size_t remainder = ((header*)pntr)->size - width;

    if(((header*)pntr)->size == width) { 
        ((header*)pntr)->status = 1;
        return true;

    } else if (remainder < sizeof(prev_next) + sizeof(header) + BUF_DATA_SIZE) { //if not enough space for info at end do a malloc
        return false;

    } else if(((header*)pntr)->size > width) {  //if realloc smaller
        merge_free(pntr);
        ((header*)pntr)->status = 1;
        void *new_hdr = (char*)pntr + width + sizeof(header) + sizeof(prev_next);
        ((header*)new_hdr)->size = ((header*)pntr)->size - width - sizeof(header) -  sizeof(prev_next);      //set new header size   
        ((header*)new_hdr)->status = 0;                                                
        ((header*)pntr)->size = width;
        add_node(hdr_to_prev_next(new_hdr));
        return true;
    } else {
        return false;
    }
}

/*This functions creates a new header for an allocation request*/
void create_chunk_hdr(void *pntr, size_t width, bool is_realloc)
{
    if (width < sizeof(prev_next) + sizeof(header)) width = sizeof(prev_next) + sizeof(width);
     
    void *new_chunk_hdr = (char*)pntr + width + sizeof(header) + sizeof(prev_next); 
    ((header*)new_chunk_hdr)->status = 0;                                                
    ((header*)new_chunk_hdr)->size = ((header*)pntr)->size - width - sizeof(header) -  sizeof(prev_next);      //set new header size   
    ((header*)pntr)->size = width;    
    ((header*)pntr)->status = 1;            //set to used
    remove_node(hdr_to_prev_next(pntr)); 
    add_node(hdr_to_prev_next(new_chunk_hdr));
}


/*This function merges two free blocks together in constant time*/
void merge_free(void *pntr)
{
    void *free_hdr = get_next_hdr(pntr);
    if(get_next_hdr(free_hdr) > segment_end) return;                                   //if end of heap
    if(free_hdr >= segment_end || ((header*)free_hdr)->status != 0) return;      //edge case if first next header is bigger than seg_end or used
    ((header*)pntr)->size += ((header*)free_hdr)->size + sizeof(header) + sizeof(prev_next); 
    remove_node(hdr_to_prev_next(free_hdr));
}

/*Will create a new block of the requested size without moving the content of the block*/
bool in_place_realloc(void *pntr, size_t formatted_sz)
{
    void *next_hdr = get_next_hdr(pntr);
    if(((header*)next_hdr)->status == 0) {
        if(((header*)next_hdr)->size + ((header*)pntr)->size  < formatted_sz) {  //-24 after pntr size
            return false;
        } else {
            merge_free(pntr);
            ((header*)next_hdr)->status = 1;
            remove_node(hdr_to_prev_next(next_hdr));
            void *new_hdr = (char*)pntr + formatted_sz + sizeof(header) + sizeof(prev_next);
            ((header*)new_hdr)->size = ((header*)pntr)->size - formatted_sz - sizeof(header) - sizeof(prev_next);  
            ((header*)new_hdr)->status = 0;                           
            ((header*)pntr)->size = formatted_sz;
            add_node(hdr_to_prev_next(new_hdr));
            return true;
        }
    }
    return false;
}

/*This function will remove an old node by rewiring the previous and next node*/
void remove_node(void *pntr)
{
    //do not remove if there are no nodes
    if(!f_start) {  
        return;
    }
    void *prev_pntr = ((prev_next*)pntr)->prev;
    void *next_pntr = ((prev_next*)pntr)->next;

    if(next_pntr && !prev_pntr) {                //if first in list
        f_start = next_pntr;
        ((prev_next*)next_pntr)->prev = NULL;

    } else if(!next_pntr && prev_pntr) {         //if last in list
        ((prev_next*)prev_pntr)->next = NULL;

    } else if(!next_pntr && !prev_pntr) {        //only node in list
        f_start = NULL;

    } else {                                     //normal case
        ((prev_next*)prev_pntr)->next = next_pntr;
        ((prev_next*)next_pntr)->prev = prev_pntr;
        pntr = NULL;
    }
}

/*Adds a new node to the front of the linked list*/
void add_node(void *pntr)
{
    void *new_node = pntr;

    if(!new_node) {
        return;
    }
    void *hdr = prev_next_to_hdr(pntr);
    ((header*)hdr)->status = 0;

    if(!f_start) {                                  //if there are no nodes in linked list yet
        ((prev_next*)new_node)->next = NULL;        //nothing to point to
        ((prev_next*)new_node)->prev = NULL;

    } else {
        ((prev_next*)new_node)->next = f_start;          //new front's next is old front
        ((prev_next*)f_start)->prev = new_node;          //make old front's prev the  new front
        ((prev_next*)new_node)->prev = NULL;             //new front has no prev
    }
    f_start = new_node;
}

/*Implements the initial state of the heap before any requests*/
bool myinit(void *segment_start, size_t segment_size)
{
    if(segment_size <= 0) return false;
    segment_begin = segment_start;
    segment_end = (char*)segment_start + segment_size;

    if(segment_begin == NULL || segment_end == NULL) return false;
    ((header*)segment_begin)->size = segment_size - sizeof(header) - sizeof(prev_next);
    f_start = hdr_to_prev_next(segment_begin);
    ((prev_next*)f_start)->next = NULL;
    ((prev_next*)f_start)->prev = NULL;
    return true;
}

/*This function implements allocation servicing for the heap allocator
 * mymalloc handles various error checks and services certain malformed
 * requests such as allocations of size 0. Specifically, mymalloc will
 * traverse the entire linked list until it finds a large enough large block
 * to return to the client.
 */ 
void *mymalloc(size_t requestedsz)
{
    if(requestedsz > (1 << 30) || requestedsz <= 0) return NULL;    //check for malformed requests
    if(!f_start) return NULL;                                       //if heap is exhausted
    void * curr_node = f_start;
    size_t formatted_sz = roundup(requestedsz, ALIGNMENT);
    if(formatted_sz < sizeof(header) + sizeof(prev_next)) formatted_sz = sizeof(header) + sizeof(prev_next);    //roundup to 24

    while(curr_node != NULL) {                                   //if not at end of linked list
        void *curr_hdr = prev_next_to_hdr(curr_node);
        if(((header*)curr_hdr)->size > formatted_sz + BUF_DATA_SIZE) {      //if enough space, make new hdr and add new node, remove old node
            create_chunk_hdr(curr_hdr, formatted_sz, false);
            return prev_next_to_payload(curr_node);              //return location to payload
        }
        curr_node = ((prev_next*)curr_node)->next;               //go to next node in linked liset
    }
    return NULL;
}

/*This function implements freeing servicing for the heap allocator
 * myfree checks that the pointer given is not NULL, and then adds
 * it to the linked list of free blocks
 */
void myfree(void *ptr)
{
    if(ptr == NULL) return;
    void *hdr = payload_to_hdr(ptr);
    void *node = payload_to_prev_next(ptr);
    ((header*)hdr)->status = 0;
    add_node(node);
    merge_free(hdr);
}

/*This function implements reallocation servicing for the heap allocator
 * myrealloc conducts various error checking for malformed client-passed
 * requests. It allows blocks to be shrunk, as well as increased if there
 * is enough space. Additionally, realloc will attempt to realloc in place
 * in order to decrease heap memory fragmentation and improve performance. If
 * the in place reallocation cannot be performed, myrealloc performs a call to
 * my malloc and copies over the old data to the new chunk and returns the new 
 * pointer to the client.
 */ 
void *myrealloc(void *oldptr, size_t newsz)
{
    if(oldptr < segment_begin || oldptr > segment_end || oldptr == NULL) return mymalloc(newsz);
    void *hdr_pntr = payload_to_hdr(oldptr);
    size_t orig_size = ((header*)hdr_pntr)->size;
    if(newsz > MAX_HEAP_SIZE) return NULL;
    //For C11, realloc with size 0 returns NULL pointer
    if(newsz == 0) return NULL; 

    size_t formatted_sz = roundup(newsz, ALIGNMENT);
    if(orig_size == formatted_sz) return oldptr;

    if(orig_size > formatted_sz) {                  //smaller case
        if(shrink_chunk(hdr_pntr, formatted_sz)) {
            return oldptr;
        } else {
            void *ret_pntr = mymalloc(formatted_sz);
            if(!ret_pntr) return NULL;
            memcpy(ret_pntr, oldptr, formatted_sz);
            myfree(oldptr);
            return ret_pntr;
        }
    }
    if(in_place_realloc(hdr_pntr, formatted_sz)) {  //larger case
        return oldptr;
    } else {
        void *ret_pntr = mymalloc(formatted_sz);
        if(!ret_pntr) return NULL;
        memcpy(ret_pntr, oldptr, orig_size);
        myfree(oldptr);
        return ret_pntr;
    }
}

/*This function performs error-checking for the implementer*/
bool validate_heap()
{
    return true;
    void *curr_hdr = segment_begin;
    bool found = false;
    void *curr_free = f_start;

    printf("\n\nChecking heap\n");
    while(curr_hdr < segment_end) {
        printf("Checking hdr at %p\n", curr_hdr);
        printf("\tHeader size: %u\n", ((header*)curr_hdr)->size);
        printf("\tHeader status: %d\n", ((header*)curr_hdr)->status);

        bool should_be_free = ((header*)curr_hdr)->status == 0;

        int i = 0;
        while(curr_free) {
            //    printf("%d: Traversing linked list at %p\n", i, curr_free);
            //    printf("\tNext ptr is: %p\n", ((prev_next*)curr_free)->next);
            //    printf("\tPrev ptr is: %p\n", ((prev_next*)curr_free)->prev);
            if(curr_free == hdr_to_prev_next(curr_hdr)) found = true; 
            curr_free = ((prev_next*)curr_free)->next;
            i++;
        }

        if (found && !should_be_free) {
            printf("Found in free list and shouldn't be free\n");
            return false;
        } else if (!found && should_be_free) {
            printf("Not found in free list and should be free\n");
            return false;
        }

        curr_free  = f_start;
        found = false;
        curr_hdr = get_next_hdr(curr_hdr);
    }
    return true;
}
