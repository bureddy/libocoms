/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2007 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2006 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2008-2011 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2012-2013 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */

#if !defined(MCA_BASE_VAR_ENUM_H)
#define MCA_BASE_VAR_ENUM_H

#include "ocoms/platform/ocoms_config.h"

#include "ocoms/util/ocoms_object.h"
#include "ocoms/platform/ocoms_constants.h"

typedef struct ocoms_mca_base_var_enum_t ocoms_mca_base_var_enum_t;

/**
 * Get the number of values in the enumerator
 *
 * @param[in] self the enumerator
 * @param[out] count the number of values in the enumerator
 */
typedef int (*ocoms_mca_base_var_enum_get_count_fn_t)(ocoms_mca_base_var_enum_t *self, int *count);

/**
 * Get the value and its string representation for an index 0..get_count()
 *
 * @param[in] self the enumerator
 * @param[in] index the index to get the value of
 * @param[out] value integer value
 * @param[out] string_value string value
 */
typedef int (*ocoms_mca_base_var_enum_get_value_fn_t)(ocoms_mca_base_var_enum_t *self, int index,
                                                int *value, const char **string_value);

/**
 * Look up the integer value of a string
 *
 * @param[in] self the enumerator
 * @param[in] string_value string to lookup
 * @param[out] value integer value for the string
 *
 * @retval OCOMS_SUCCESS if found
 * @retval OCOMS_ERR_VALUE_OUT_OF_BOUNDS if not
 */
typedef int (*ocoms_mca_base_var_enum_vfs_fn_t)(ocoms_mca_base_var_enum_t *self, const char *string_value,
                                          int *value);

/**
 * Dump a textual representation of all the values in an enumerator
 *
 * @param[in] self the enumerator
 * @param[out] out the string representation
 *
 * @retval OCOMS_SUCCESS on success
 * @retval ocoms error on error
 */
typedef int (*ocoms_mca_base_var_enum_dump_fn_t)(ocoms_mca_base_var_enum_t *self, char **out);

/**
 * Get the string representation for an enumerator value
 *
 * @param[in] self the enumerator
 * @param[in] value integer value
 * @param[out] string_value string value for value
 *
 * @retval OCOMS_SUCCESS on success
 * @retval OCOMS_ERR_VALUE_OUT_OF_BOUNDS if not found
 */
typedef int (*ocoms_mca_base_var_enum_sfv_fn_t)(ocoms_mca_base_var_enum_t *self, const int value,
                                          const char **string_value);

/**
 * The default enumerator class takes in a list of integer-string pairs. If a
 * string is read from an environment variable or a file value the matching
 * integer value is used for the MCA variable.
 */
struct ocoms_mca_base_var_enum_value_t {
    int value;
    const char *string;
};

typedef struct ocoms_mca_base_var_enum_value_t ocoms_mca_base_var_enum_value_t;

/* enumerator base class */
struct ocoms_mca_base_var_enum_t {
    ocoms_object_t super;

    /** Name of this enumerator. This value is duplicated from the argument provided to
        ocoms_mca_base_var_enum_create() */
    char *enum_name;

    /** Get the number of values this enumerator represents. Subclasses should override
        the default function. */
    ocoms_mca_base_var_enum_get_count_fn_t get_count;
    /** Get the value and string representation for a particular index. Subclasses should
        override the default function */
    ocoms_mca_base_var_enum_get_value_fn_t get_value;
    /** Given a string return corresponding integer value. If the string does not match a
     valid value return OCOMS_ERR_VALUE_OUT_OF_BOUNDS */
    ocoms_mca_base_var_enum_vfs_fn_t value_from_string;
    /** Given an integer return the corresponding string value. If the integer does not
        match a valid value return OCOMS_ERR_VALUE_OUT_OF_BOUNDS */
    ocoms_mca_base_var_enum_sfv_fn_t string_from_value;
    /** Dump a textual representation of the enumerator. The caller is responsible for
        freeing the string */
    ocoms_mca_base_var_enum_dump_fn_t dump;

    int enum_value_count;
    /** Copy of the enumerators values (used by the default functions). This array and
        and the strings it contains are freed by the destructor if not NULL. */
    ocoms_mca_base_var_enum_value_t *enum_values;
};

/**
 * Object declaration for ocoms_mca_base_var_enum_t
 */
OCOMS_DECLSPEC OBJ_CLASS_DECLARATION(ocoms_mca_base_var_enum_t);

/**
 * Create a new default enumerator
 *
 * @param[in] name Name for this enumerator
 * @param[in] values List of values terminated with a NULL .string
 * member.
 * @param[out] enumerator Newly created enumerator.
 *
 * @retval OCOMS_SUCCESS On success
 * @retval ocoms error code On error
 *
 * This function creates a value enumerator for integer variables. The
 * value array is stored by reference in the enumerator so it should
 * not be allocated on the stack. The OUT enumerator value will be a
 * newly OBJ_NEW'ed object that should be released by the caller via
 * OBJ_RELEASE.
 *
 * Note that the output enumerator can be OBJ_RELEASE'd after it has
 * been used in a pvar registration, because variables that use the
 * enumerator will OBJ_RETAIN it.
 */
OCOMS_DECLSPEC int ocoms_mca_base_var_enum_create (const char *name, const ocoms_mca_base_var_enum_value_t values[],
                                            ocoms_mca_base_var_enum_t **enumerator);

#endif /* !defined(MCA_BASE_VAR_ENUM_H) */
