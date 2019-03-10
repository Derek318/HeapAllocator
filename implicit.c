/* Name: Derek McCreight
 * =====================
 * Description: This program implements an implicit heap allocator as apart of the
 * final capstone project for Stanford's CS107 class. The implicit heap allocator 
 * supports memory allocation, reallocation, and freeing. It maintains memory block
 * headers in order to traverse the heap structure. The implicit heap allocator's 
 * efficient is not optimal because the entire heap structure must be traverse in 
 * order to service client-called requests.
 * =====================
 */ 
#include "allocator.h"
#include "debug_break.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define ALIGNMENT 8
#define MINIMUM_SIZE 1
#define MAX_HEAP_SIZE 1<<30

static void *segment_begin;
static void *segment_end;

typedef struct header 
{
    int size;
    int status;    //value of 1 is used and 0 is unused
} header;

void create_chunk_hdr(void *pntr, size_t width);
void *get_next_hdr(void *pntr);
void *hdr_to_payload(void *pntr);
void *payload_to_hdr(void*pntr);

/*Adapted from bump allocator-- this function rounds up to the nearest multiple.*/
size_t roundup(size_t size, size_t mult)
{
    return (size + mult - 1) & ~(mult-1);
}

void *get_next_hdr(void *pntr)
{
    return (char*)pntr + ((header*)pntr)->size + sizeof(header);
}

void *hdr_to_payload(void *pntr)
{
    return (char*)pntr + sizeof(header);
}

void *payload_to_hdr(void *pntr)
{
    return (char*)pntr - sizeof(header); 
}

/* This function initializes the beginning and end of the
 * heap and creates the first header in the heap.
 */ 
bool myinit(void *segment_start, size_t segment_size)
{
    if(segment_size <= 0) return false;                               //cannot accept 0 bytes or negative

    segment_begin = segment_start;
    segment_end = (char*)segment_start + segment_size;                //initialize global begin/end

    if(segment_begin == NULL || segment_end == NULL) return false;

    ((header*)segment_begin)->size = segment_size - sizeof(header);   //size of heap - first header size
    ((header*)segment_begin)->status = 0;   
    return true;
}

/* This function takes a requested size and returns a pointer to a chunk
 * of memory of at least the requested size. If the memory chunk cannot be
 * allocated, mymyalloc will return NULL. During allocation, header information
 * and "bookkeeping" is updated upon a successful allocation request. The mymalloc
 * function traveres both used and unused chunks, thus implementing an "implicit" 
 * heap allocator.
 */ 
void *mymalloc(size_t requestedsz)
{
    if(requestedsz > (1 << 30) || requestedsz <= 0) return NULL;        //check for bad requests
    size_t formatted_sz = roundup(requestedsz, ALIGNMENT);              //round to nearest 8 bytes
    void *pntr = segment_begin;

    while(pntr < segment_end) //iterate through the heap
    {
        size_t chunk_sz = ((header*)pntr)->size;
        int chunk_status = ((header*)pntr)->status; 

        if(chunk_sz >= formatted_sz && chunk_status == 0) {     //if free and big enough
            ((header*)pntr)->status = 1;                        //mark as used chunk
            create_chunk_hdr(pntr, formatted_sz);               //make next header

            return hdr_to_payload(pntr);                        //return payload
        }
        pntr = get_next_hdr(pntr);
    }
    return NULL;
}

/* This function performs the necessary pointer arithmetic to
 * find the location of the next header, create it, and
 * update the original header information
 */ 
void create_chunk_hdr(void *pntr, size_t width)
{
    width = roundup(width, ALIGNMENT);
    void *new_chunk_hdr = (char*)pntr + width + sizeof(header);
    if(((header*)pntr)->size == width) return;
    ((header*)new_chunk_hdr)->status = 0;                                                 //set to unused
    ((header*)new_chunk_hdr)->size = ((header*)pntr)->size - sizeof(header) - width;      //set new header size
    ((header*)pntr)->size = width;                                                        //change allocated header

}

/* This function finds the header of the given pointer, checks
 * for input errors, and then sets status to unused if input is valid.
 */ 
void myfree(void *ptr)
{
    if(ptr == NULL || ptr < segment_begin || ptr > segment_end ) return;    //if malformed, do nothing
    void *header_ptr = payload_to_hdr(ptr);

    ((header*)header_ptr)->status = 0;        //set status--unused
}

/* Takes as input the original pointer and the new requested size. It then
 * determines whether the request is malformed. This implementation will 
 * malloc a new request if the original pointer was not heap allocated, or NULL.
 * If the requested size is less than the original memory size of the provided 
 * location, then realloc will shrink the original block by calling the function
 * create_chunk_hdr. Additionally, myrealloc will 
 */
void *myrealloc(void *oldptr, size_t newsz)
{
    if(newsz > MAX_HEAP_SIZE) return NULL;
    if(oldptr < segment_begin || oldptr > segment_end || oldptr == NULL) {
        return  mymalloc(newsz);
    }
    void *header_ptr = payload_to_hdr(oldptr);
    size_t orig_sz = ((header*)header_ptr)->size;

    if(newsz == 0) {
        void* ret_pntr = mymalloc(MINIMUM_SIZE);   //if request is 0 bytes, allocate 1 byte.
        assert(ret_pntr != NULL);
        myfree(oldptr);
        return ret_pntr;
    }
    if(newsz == orig_sz) {return oldptr;} 
    size_t formatted_sz = roundup(newsz, ALIGNMENT);

    // If client wants to reallocate with less memory,
    // it is the same as shrinking the chunk and freeing the remaining memory
    if(orig_sz > newsz) {
        create_chunk_hdr(header_ptr, formatted_sz);
        return hdr_to_payload(header_ptr);

    }
    void *ret_pntr = mymalloc(formatted_sz); //malloc new bigger chunk
    if(ret_pntr == NULL) {return NULL;}
    memcpy(ret_pntr, oldptr, orig_sz);
    myfree(oldptr);                          //free old chunk
    return ret_pntr;
}

/* Performs heap error and validity checks*/
bool validate_heap()
{
    void *curr_hdr = segment_begin;
    void *end_of_heap = segment_end;
    int curr_status = ((header*)curr_hdr)->status;
    void *next_hdr = get_next_hdr(curr_hdr);

    if(curr_status != 0 && curr_status != 1) {
        printf("Malformed status");
        return false;
    }

    if(next_hdr < end_of_heap) {
        if(((header*)next_hdr)->status != 1 && ((header*)next_hdr)->status != 0) {
            printf("Unaligned chunk headers");
            return false;
        }
    }
    curr_hdr = next_hdr;
    return true;
}
