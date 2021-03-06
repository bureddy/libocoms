/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2008 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011-2013 UT-Battelle, LLC. All rights reserved.
 * Copyright (C) 2013      Mellanox Technologies Ltd. All rights reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */
/** @file
 *
 * See ocoms_bitmap.h for an explanation of why there is a split
 * between OPAL and ORTE for this generic class.
 *
 * Utility functions to manage fortran <-> c opaque object
 * translation.  Note that since MPI defines fortran handles as
 * [signed] int's, we use int everywhere in here where you would
 * normally expect size_t.  There's some code that makes sure indices
 * don't go above FORTRAN_HANDLE_MAX (which is min(INT_MAX, fortran
 * INTEGER max)), just to be sure.
 */

#ifndef OCOMS_POINTER_ARRAY_H
#define OCOMS_POINTER_ARRAY_H

#include "ocoms/platform/ocoms_config.h"

#include "ocoms/threads/mutex.h"
#include "ocoms/util/ocoms_object.h"

BEGIN_C_DECLS

/**
 * dynamic pointer array
 */
struct ocoms_pointer_array_t {
    /** base class */
    ocoms_object_t super;
    /** synchronization object */
    ocoms_mutex_t lock;
    /** Index of lowest free element.  NOTE: This is only an
        optimization to know where to search for the first free slot.
        It does \em not necessarily imply indices all above this index
        are not taken! */
    int lowest_free;
    /** number of free elements in the list */
    int number_free;
    /** size of list, i.e. number of elements in addr */
    int size;
    /** maximum size of the array */
    int max_size;
    /** block size for each allocation */
    int block_size;
    /** pointer to array of pointers */
    void **addr;
};
/**
 * Convenience typedef
 */
typedef struct ocoms_pointer_array_t ocoms_pointer_array_t;
/**
 * Class declaration
 */
OCOMS_DECLSPEC OBJ_CLASS_DECLARATION(ocoms_pointer_array_t);

/**
 * Initialize the pointer array with an initial size of initial_allocation.
 * Set the maximum size of the array, as well as the size of the allocation
 * block for all subsequent growing operations. Remarque: The pointer array
 * has to be created bfore calling this function.
 *
 * @param array Pointer to pointer of an array (IN/OUT)
 * @param initial_allocation The number of elements in the initial array (IN)
 * @param max_size The maximum size of the array (IN)
 * @param block_size The size for all subsequent grows of the array (IN).
 *
 * @return OCOMS_SUCCESS if all initializations were succesfull. Otherwise,
 *  the error indicate what went wrong in the function.
 */
OCOMS_DECLSPEC int ocoms_pointer_array_init( ocoms_pointer_array_t* array,
                                           int initial_allocation,
                                           int max_size, int block_size );

/**
 * Add a pointer to the array (Grow the array, if need be)
 *
 * @param array Pointer to array (IN)
 * @param ptr Pointer value (IN)
 *
 * @return Index of inserted array element.  Return value of
 *  (-1) indicates an error.
 */
OCOMS_DECLSPEC int ocoms_pointer_array_add(ocoms_pointer_array_t *array, void *ptr);

/**
 * Set the value of an element in array
 *
 * @param array Pointer to array (IN)
 * @param index Index of element to be reset (IN)
 * @param value New value to be set at element index (IN)
 *
 * @return Error code.  (-1) indicates an error.
 */
OCOMS_DECLSPEC int ocoms_pointer_array_set_item(ocoms_pointer_array_t *array, 
                                int index, void *value);

/**
 * Get the value of an element in array
 *
 * @param array          Pointer to array (IN)
 * @param element_index  Index of element to be returned (IN)
 *
 * @return Error code.  NULL indicates an error.
 */

static inline void *ocoms_pointer_array_get_item(ocoms_pointer_array_t *table, 
                                                int element_index)
{
    void *p;

    if( table->size <= element_index ) {
        return NULL;
    }
    OCOMS_THREAD_LOCK(&(table->lock));
    p = table->addr[element_index];
    OCOMS_THREAD_UNLOCK(&(table->lock));
    return p;
}


/**
 * Get the size of the pointer array
 *
 * @param array Pointer to array (IN)
 *
 * @returns size Size of the array
 *
 * Simple inline function to return the size of the array in order to
 * hide the member field from external users.
 */
static inline int ocoms_pointer_array_get_size(ocoms_pointer_array_t *array)
{
  return array->size;
}

/**
 * Set the size of the pointer array
 *
 * @param array Pointer to array (IN)
 *
 * @param size Desired size of the array
 *
 * Simple function to set the size of the array in order to
 * hide the member field from external users.
 */
OCOMS_DECLSPEC int ocoms_pointer_array_set_size(ocoms_pointer_array_t *array, int size);

/**
 * Test whether a certain element is already in use. If not yet
 * in use, reserve it.
 *
 * @param array Pointer to array (IN)
 * @param index Index of element to be tested (IN)
 * @param value New value to be set at element index (IN)
 *
 * @return true/false True if element could be reserved
 *                    False if element could not be reserved (e.g., in use).
 *
 * In contrary to array_set, this function does not allow to overwrite 
 * a value, unless the previous value is NULL ( equiv. to free ).
 */
OCOMS_DECLSPEC bool ocoms_pointer_array_test_and_set_item (ocoms_pointer_array_t *table, 
                                          int index,
                                          void *value);

/**
 * Empty the array.
 *
 * @param array Pointer to array (IN)
 *
 */
static inline void ocoms_pointer_array_remove_all(ocoms_pointer_array_t *array)
{
    int i;
    if( array->number_free == array->size )
        return;  /* nothing to do here this time (the array is already empty) */
 
    OCOMS_THREAD_LOCK(&array->lock);
    array->lowest_free = 0;
    array->number_free = array->size;
    for(i=0; i<array->size; i++) {
        array->addr[i] = NULL;
    }
    OCOMS_THREAD_UNLOCK(&array->lock);
}

END_C_DECLS

#endif /* OCOMS_POINTER_ARRAY_H */
