/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (c) 2004-2009 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2009      Oak Ridge National Labs.  All rights reserved.
 * Copyright (c) 2011-2013 UT-Battelle, LLC. All rights reserved.
 * Copyright (C) 2013      Mellanox Technologies Ltd. All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ocoms/platform/ocoms_config.h"
#include "ocoms/datatype/ocoms_datatype.h"
#include "ocoms/datatype/ocoms_convertor.h"
#include "ocoms/datatype/ocoms_datatype_internal.h"

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

/* Get the number of elements from the data-type that can be
 * retrieved from a received buffer with the size iSize.
 * To speed-up this function you should use it with a iSize == to the modulo
 * of the original size and the size of the data.
 * Return value:
 *   positive = number of basic elements inside
 *   negative = some error occurs
 */
ssize_t ocoms_datatype_get_element_count( const ocoms_datatype_t* datatype, size_t iSize )
{
    dt_stack_t* pStack;   /* pointer to the position on the stack */
    uint32_t pos_desc;    /* actual position in the description of the derived datatype */
    ssize_t nbElems = 0, stack_pos = 0;
    size_t local_size;
    dt_elem_desc_t* pElems;

    /* Normally the size should be less or equal to the size of the datatype.
     * This function does not support a iSize bigger than the size of the datatype.
     */
    assert( (uint32_t)iSize <= datatype->size );
    DUMP( "dt_count_elements( %p, %d )\n", (void*)datatype, iSize );
    pStack = (dt_stack_t*)alloca( sizeof(dt_stack_t) * (datatype->btypes[OCOMS_DATATYPE_LOOP] + 2) );
    pStack->count    = 1;
    pStack->index    = -1;
    pStack->disp     = 0;
    pElems           = datatype->desc.desc;
    pos_desc         = 0;

    while( 1 ) {  /* loop forever the exit condition is on the last OCOMS_DATATYPE_END_LOOP */
        if( OCOMS_DATATYPE_END_LOOP == pElems[pos_desc].elem.common.type ) { /* end of the current loop */
            if( --(pStack->count) == 0 ) { /* end of loop */
                stack_pos--; pStack--;
                if( stack_pos == -1 ) return nbElems;  /* completed */
            }
            pos_desc = pStack->index + 1;
            continue;
        }
        if( OCOMS_DATATYPE_LOOP == pElems[pos_desc].elem.common.type ) {
            ddt_loop_desc_t* loop = &(pElems[pos_desc].loop);
            do {
                PUSH_STACK( pStack, stack_pos, pos_desc, OCOMS_DATATYPE_LOOP, loop->loops, 0 );
                pos_desc++;
            } while( OCOMS_DATATYPE_LOOP == pElems[pos_desc].elem.common.type ); /* let's start another loop */
            DDT_DUMP_STACK( pStack, stack_pos, pElems, "advance loops" );
        }
        while( pElems[pos_desc].elem.common.flags & OCOMS_DATATYPE_FLAG_DATA ) {
            /* now here we have a basic datatype */
            const ocoms_datatype_t* basic_type = BASIC_DDT_FROM_ELEM(pElems[pos_desc]);
            local_size = pElems[pos_desc].elem.count * basic_type->size;
            if( local_size >= iSize ) {
                local_size = iSize / basic_type->size;
                nbElems += (int32_t)local_size;
                iSize -= local_size * basic_type->size;
                return (iSize == 0 ? nbElems : -1);
            }
            nbElems += pElems[pos_desc].elem.count;
            iSize -= local_size;
            pos_desc++;  /* advance to the next data */
        }
    }
}

int32_t ocoms_datatype_set_element_count( const ocoms_datatype_t* datatype, size_t count, size_t* length )
{
    dt_stack_t* pStack;   /* pointer to the position on the stack */
    size_t pos_desc;    /* actual position in the description of the derived datatype */
    int32_t stack_pos = 0;
    size_t local_length = 0;
    dt_elem_desc_t* pElems;

    /**
     * Handle all complete multiple of the datatype.
     */
    for( pos_desc = 4; pos_desc < OCOMS_DATATYPE_MAX_PREDEFINED; pos_desc++ ) {
        local_length += datatype->btypes[pos_desc];
    }
    pos_desc = count / local_length;
    count = count % local_length;
    *length = datatype->size * pos_desc;
    if( 0 == count ) {
        return 0;
    }

    DUMP( "dt_set_element_count( %p, %d )\n", (void*)datatype, count );
    pStack = (dt_stack_t*)alloca( sizeof(dt_stack_t) * (datatype->btypes[OCOMS_DATATYPE_LOOP] + 2) );
    pStack->count    = 1;
    pStack->index    = -1;
    pStack->disp     = 0;
    pElems           = datatype->desc.desc;
    pos_desc         = 0;

    while( 1 ) {  /* loop forever the exit condition is on the last OCOMS_DATATYPE_END_LOOP */
        if( OCOMS_DATATYPE_END_LOOP == pElems[pos_desc].elem.common.type ) { /* end of the current loop */
            if( --(pStack->count) == 0 ) { /* end of loop */
                stack_pos--; pStack--;
                if( stack_pos == -1 ) return 0;
            }
            pos_desc = pStack->index + 1;
            continue;
        }
        if( OCOMS_DATATYPE_LOOP == pElems[pos_desc].elem.common.type ) {
            ddt_loop_desc_t* loop = &(pElems[pos_desc].loop);
            do {
                PUSH_STACK( pStack, stack_pos, pos_desc, OCOMS_DATATYPE_LOOP, loop->loops, 0 );
                pos_desc++;
            } while( OCOMS_DATATYPE_LOOP == pElems[pos_desc].elem.common.type ); /* let's start another loop */
            DDT_DUMP_STACK( pStack, stack_pos, pElems, "advance loops" );
        }
        while( pElems[pos_desc].elem.common.flags & OCOMS_DATATYPE_FLAG_DATA ) {
            /* now here we have a basic datatype */
            const ocoms_datatype_t* basic_type = BASIC_DDT_FROM_ELEM(pElems[pos_desc]);
            local_length = pElems[pos_desc].elem.count;
            if( local_length >= count ) {
                *length += count * basic_type->size;
                return 0;
            }
            *length += local_length * basic_type->size;
            count -= local_length;
            pos_desc++;  /* advance to the next data */
        }
    }
}

