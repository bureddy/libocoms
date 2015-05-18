/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2008 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2012 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart, 
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2008-2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2012-2013 Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 * 
 * Additional copyrights may follow
 * 
 * $HEADER$
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#include <errno.h>

#include "ocoms/platform/ocoms_config.h"
#include "ocoms/platform/ocoms_stdint.h"
#include "ocoms/util/os_path.h"
#include "ocoms/util/path.h"
#if 0
#include "ocoms/mca/installdirs/installdirs.h"
#include "ocoms/util/show_help.h"
#include "ocoms/runtime/ocoms.h"
#endif
#include "ocoms/util/printf.h"
#include "ocoms/util/argv.h"
#include "ocoms/mca/mca.h"
#include "ocoms/mca/base/mca_base_vari.h"
#include "ocoms/platform/ocoms_constants.h"
#include "ocoms/util/output.h"
#include "ocoms/util/ocoms_environ.h"

/*
 * local variables
 */
static ocoms_pointer_array_t ocoms_mca_base_vars;
static const char *mca_prefix = "OMPI_MCA_";
static char *home = NULL;
static char *cwd  = NULL;
bool ocoms_mca_base_var_initialized = false;
static char * force_agg_path = NULL;
static char *ocoms_mca_base_var_files = NULL;
static char **ocoms_mca_base_var_file_list = NULL;
static char *ocoms_mca_base_var_override_file = NULL;
static char *ocoms_mca_base_var_file_prefix = NULL;
static char *ocoms_mca_base_param_file_path = NULL;
static bool ocoms_mca_base_var_suppress_override_warning = false;
static ocoms_list_t ocoms_mca_base_var_file_values;
static ocoms_list_t ocoms_mca_base_var_override_values;

static int ocoms_mca_base_var_count = 0;

static ocoms_hash_table_t ocoms_mca_base_var_index_hash;

const char *var_type_names[] = {
    "int",
    "unsigned",
    "unsigned long",
    "unsigned long long",
    "size_t",
    "string",
    "bool",
    "double"
};

const char *var_type_formats[] = {
    "%d",
    "%u",
    "%lu",
    "%llu",
    "%" PRIsize_t,
    "%s",
    "%d",
    "%lf"
};

const size_t var_type_sizes[] = {
    sizeof (int),
    sizeof (unsigned),
    sizeof (unsigned long),
    sizeof (unsigned long long),
    sizeof (size_t),
    sizeof (char),
    sizeof (bool),
    sizeof (double)
};

const char *var_source_names[] = {
    "default",
    "command line",
    "environment",
    "file",
    "set",
    "override"
};


static const char *info_lvl_strings[] = {
    "user/basic",
    "user/detail",
    "user/all",
    "tuner/basic",
    "tuner/detail",
    "tuner/all",
    "dev/basic",
    "dev/detail",
    "dev/all"
};

/*
 * local functions
 */
static int fixup_files(char **file_list, char * path, bool rel_path_search);
static int read_files (char *file_list, ocoms_list_t *file_values);
static int ocoms_mca_base_var_cache_files (bool rel_path_search);
static int var_set_initial (ocoms_mca_base_var_t *var);
static int var_get (int index, ocoms_mca_base_var_t **var_out, bool original);
static int var_value_string (ocoms_mca_base_var_t *var, char **value_string);

/*
 * classes
 */
static void var_constructor (ocoms_mca_base_var_t *p);
static void var_destructor (ocoms_mca_base_var_t *p);
OBJ_CLASS_INSTANCE(ocoms_mca_base_var_t, ocoms_object_t, 
                   var_constructor, var_destructor);

static void fv_constructor (ocoms_mca_base_var_file_value_t *p);
static void fv_destructor (ocoms_mca_base_var_file_value_t *p);
OBJ_CLASS_INSTANCE(ocoms_mca_base_var_file_value_t, ocoms_list_item_t,
                   fv_constructor, fv_destructor);

/*
 * Generate a full name from three names
 */
int ocoms_mca_base_var_generate_full_name4 (const char *project, const char *framework, const char *component,
                                      const char *variable, char **full_name)
{
    const char * const names[] = {project, framework, component, variable};
    char *name, *tmp;
    size_t i, len;

    *full_name = NULL;

    for (i = 0, len = 0 ; i < 4 ; ++i) {
        if (NULL != names[i]) {
            /* Add space for the string + _ or \0 */
            len += strlen (names[i]) + 1;
        }
    }

    name = calloc (1, len);
    if (NULL == name) {
        return OCOMS_ERR_OUT_OF_RESOURCE;
    }

    for (i = 0, tmp = name ; i < 4 ; ++i) {
        if (NULL != names[i]) {
            if (name != tmp) {
                *tmp++ = '_';
            }
            strncat (name, names[i], len - (size_t)(uintptr_t)(tmp - name));
            tmp += strlen (names[i]);
        }
    }

    *full_name = name;
    return OCOMS_SUCCESS;
}

static int compare_strings (const char *str1, const char *str2) {
    if ((NULL != str1 && 0 == strcmp (str1, "*")) ||
        (NULL == str1 && NULL == str2)) {
        return 0;
    } 

    if (NULL != str1 && NULL != str2) {
        return strcmp (str1, str2);
    }

    return 1;
}

/*
 * Append a filename to the file list if it does not exist and return a
 * pointer to the filename in the list.
 */
static char *append_filename_to_list(const char *filename)
{
    int i, count;

    (void) ocoms_argv_append_unique_nosize(&ocoms_mca_base_var_file_list, filename, false);

    count = ocoms_argv_count(ocoms_mca_base_var_file_list);

    for (i = count - 1; i >= 0; --i) {
        if (0 == strcmp (ocoms_mca_base_var_file_list[i], filename)) {
            return ocoms_mca_base_var_file_list[i];
        }
    }

    /* *#@*? */
    return NULL;
}

/*
 * Set it up
 */
int ocoms_mca_base_var_init(void)
{
    int ret;

    if (!ocoms_mca_base_var_initialized) {
        /* Init the value array for the param storage */

        OBJ_CONSTRUCT(&ocoms_mca_base_vars, ocoms_pointer_array_t);
        /* These values are arbitrary */
        ret = ocoms_pointer_array_init (&ocoms_mca_base_vars, 128, 16384, 128);
        if (OCOMS_SUCCESS != ret) {
            return ret;
        }

        ocoms_mca_base_var_count = 0;

        /* Init the file param value list */

        OBJ_CONSTRUCT(&ocoms_mca_base_var_file_values, ocoms_list_t);
        OBJ_CONSTRUCT(&ocoms_mca_base_var_override_values, ocoms_list_t);
        OBJ_CONSTRUCT(&ocoms_mca_base_var_index_hash, ocoms_hash_table_t);

        ret = ocoms_hash_table_init (&ocoms_mca_base_var_index_hash, 1024);
        if (OCOMS_SUCCESS != ret) {
            return ret;
        }

        ret = ocoms_mca_base_var_group_init ();
        if  (OCOMS_SUCCESS != ret) {
            return ret;
        }

        ret = ocoms_mca_base_pvar_init ();
        if (OCOMS_SUCCESS != ret) {
            return ret;
        }

        /* Set this before we register the parameter, below */

        ocoms_mca_base_var_initialized = true; 

        ocoms_mca_base_var_cache_files(false);
    }

    return OCOMS_SUCCESS;
}

static int ocoms_mca_base_var_cache_files(bool rel_path_search)
{
    char *tmp;
    int ret;

    /* We may need this later */
    home = (char*)ocoms_home_directory();
    
    if(NULL == cwd) {
        cwd = (char *) malloc(sizeof(char) * MAXPATHLEN);
        if( NULL == (cwd = getcwd(cwd, MAXPATHLEN) )) {
            ocoms_output(0, "Error: Unable to get the current working directory\n");
            cwd = strdup(".");
        }
    }

#if OCOMS_WANT_HOME_CONFIG_FILES
    asprintf(&ocoms_mca_base_var_files, "%s"OCOMS_PATH_SEP".openmpi" OCOMS_PATH_SEP
             "mca-params.conf%c%s" OCOMS_PATH_SEP "openmpi-mca-params.conf",
             home, OCOMS_ENV_SEP, ocoms_install_dirs.sysconfdir);
#else
    asprintf(&ocoms_mca_base_var_files, "%s" OCOMS_PATH_SEP "openmpi-mca-params.conf",
             ocoms_install_dirs.sysconfdir);
#endif

    /* Initialize a parameter that says where MCA param files can be found.
       We may change this value so set the scope to MCA_BASE_VAR_SCOPE_READONLY */
    tmp = ocoms_mca_base_var_files;
    ret = ocoms_mca_base_var_register ("ocoms", "mca", "base", "param_files", "Path for MCA "
                                 "configuration files containing variable values",
                                 MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OCOMS_INFO_LVL_2,
                                 MCA_BASE_VAR_SCOPE_READONLY, &ocoms_mca_base_var_files);
    free (tmp);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    (void) ocoms_mca_base_var_register_synonym (ret, "ocoms", "mca", NULL, "param_files",
                                          MCA_BASE_VAR_SYN_FLAG_DEPRECATED);

    ret = asprintf(&ocoms_mca_base_var_override_file, "%s" OCOMS_PATH_SEP "openmpi-mca-params-override.conf",
                   ocoms_install_dirs.sysconfdir);
    if (0 > ret) {
        return OCOMS_ERR_OUT_OF_RESOURCE;
    }

    tmp = ocoms_mca_base_var_override_file;
    ret = ocoms_mca_base_var_register ("ocoms", "mca", "base", "override_param_file",
                                 "Variables set in this file will override any value set in"
                                 "the environment or another configuration file",
                                 MCA_BASE_VAR_TYPE_STRING, NULL, 0, MCA_BASE_VAR_FLAG_DEFAULT_ONLY,
                                 OCOMS_INFO_LVL_2, MCA_BASE_VAR_SCOPE_CONSTANT,
                                 &ocoms_mca_base_var_override_file);
    free (tmp);
    if (0 > ret) {
        return ret;
    }

    /* Disable reading MCA parameter files. */
    if (0 == strcmp (ocoms_mca_base_var_files, "none")) {
        return OCOMS_SUCCESS;
    }

    ocoms_mca_base_var_suppress_override_warning = false;
    ret = ocoms_mca_base_var_register ("ocoms", "mca", "base", "suppress_override_warning",
                                 "Suppress warnings when attempting to set an overridden value (default: false)",
                                 MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0, OCOMS_INFO_LVL_2,
                                 MCA_BASE_VAR_SCOPE_LOCAL, &ocoms_mca_base_var_suppress_override_warning);

    /* Aggregate MCA parameter files
     * A prefix search path to look up aggregate MCA parameter file
     * requests that do not specify an absolute path
     */
    ocoms_mca_base_var_file_prefix = NULL;
    ret = ocoms_mca_base_var_register ("ocoms", "mca", "base", "param_file_prefix",
                                 "Aggregate MCA parameter file sets",
                                 MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OCOMS_INFO_LVL_3,
                                 MCA_BASE_VAR_SCOPE_READONLY, &ocoms_mca_base_var_file_prefix);

    ret = asprintf(&ocoms_mca_base_param_file_path, "%s" OCOMS_PATH_SEP "amca-param-sets%c%s",
                   ocoms_install_dirs.pkgdatadir, OCOMS_ENV_SEP, cwd);
    if (0 > ret) {
        return OCOMS_ERR_OUT_OF_RESOURCE;
    }

    tmp = ocoms_mca_base_param_file_path;
    ret = ocoms_mca_base_var_register ("ocoms", "mca", "base", "param_file_path",
                                 "Aggregate MCA parameter Search path",
                                 MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OCOMS_INFO_LVL_3,
                                 MCA_BASE_VAR_SCOPE_READONLY, &ocoms_mca_base_param_file_path);
    free (tmp);
    if (0 > ret) {
        return ret;
    }

    force_agg_path = NULL;
    ret = ocoms_mca_base_var_register ("ocoms", "mca", "base", "param_file_path_force",
                                 "Forced Aggregate MCA parameter Search path",
                                 MCA_BASE_VAR_TYPE_STRING, NULL, 0, 0, OCOMS_INFO_LVL_3,
                                 MCA_BASE_VAR_SCOPE_READONLY, &force_agg_path);
    if (0 > ret) {
        return ret;
    }

    if (NULL != force_agg_path) {
        if (NULL != ocoms_mca_base_param_file_path) {
            char *tmp_str = ocoms_mca_base_param_file_path;

            asprintf(&ocoms_mca_base_param_file_path, "%s%c%s", force_agg_path, OCOMS_ENV_SEP, tmp_str);
            free(tmp_str);
        } else {
            ocoms_mca_base_param_file_path = strdup(force_agg_path);
        }
    }

    if (NULL != ocoms_mca_base_var_file_prefix) {
        char *tmp_str;
        
        /*
         * Resolve all relative paths.
         * the file list returned will contain only absolute paths
         */
        if( OCOMS_SUCCESS != fixup_files(&ocoms_mca_base_var_file_prefix, ocoms_mca_base_param_file_path, rel_path_search) ) {
#if 0
            /* JJH We need to die! */
            abort();
#else
            ;
#endif
        }
        else {
            /* Prepend the files to the search list */
            asprintf(&tmp_str, "%s%c%s", ocoms_mca_base_var_file_prefix, OCOMS_ENV_SEP, ocoms_mca_base_var_files);
            free (ocoms_mca_base_var_files);
            ocoms_mca_base_var_files = tmp_str;
        }
    }

    read_files (ocoms_mca_base_var_files, &ocoms_mca_base_var_file_values);

    if (0 == access(ocoms_mca_base_var_override_file, F_OK)) {
        read_files (ocoms_mca_base_var_override_file, &ocoms_mca_base_var_override_values);
    }

    return OCOMS_SUCCESS;
}

/*
 * Look up an integer MCA parameter.
 */
int ocoms_mca_base_var_get_value (int index, const void *value,
                            ocoms_mca_base_var_source_t *source,
                            const char **source_file)
{
    ocoms_mca_base_var_t *var;
    void **tmp = (void **) value;
    int ret;

    ret = var_get (index, &var, true);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    if (!VAR_IS_VALID(var[0])) {
        return OCOMS_ERR_BAD_PARAM;
    }

    if (NULL != value) {
        /* Return a poiner to our backing store (either a char **, int *,
           or bool *) */
        *tmp = var->mbv_storage;
    }

    if (NULL != source) {
        *source = var->mbv_source;
    }

    if (NULL != source_file) {
        *source_file = var->mbv_source_file;
    }

    return OCOMS_SUCCESS;
}

static int var_set_string (ocoms_mca_base_var_t *var, char *value)
{
    char *tmp, *p=NULL;
    int ret;

    if (NULL != var->mbv_storage->stringval) {
        free (var->mbv_storage->stringval);
    }

    var->mbv_storage->stringval = NULL;

    if (NULL == value || 0 == strlen (value)) {
        return OCOMS_SUCCESS;
    }

    /* Replace all instances of ~/ in a path-style string with the 
       user's home directory. This may be handled by the enumerator
       in the future. */
    if (0 == strncmp (value, "~/", 2)) {
        if (NULL != home) {
            ret = asprintf (&value, "%s/%s", home, value + 2);
            if (0 > ret) {
                return OCOMS_ERROR;
            }
        } else {
            value = strdup (value + 2);
        }
    } else {
        value = strdup (value);
    }

    if (NULL == value) {
        return OCOMS_ERR_OUT_OF_RESOURCE;
    }

    while (NULL != (tmp = strstr (value, ":~/"))) {
        tmp[0] = '\0';
        tmp += 3;

        ret = asprintf (&tmp, "%s:%s%s%s",
                        (NULL == p) ? "" : p,
                        home ? home : "", home ? "/" : "", tmp);

        free (value);

        if (0 > ret) {
            return OCOMS_ERR_OUT_OF_RESOURCE;
        }

        value = tmp;
    }

    var->mbv_storage->stringval = value;

    return OCOMS_SUCCESS;
}

static int int_from_string(const char *src, ocoms_mca_base_var_enum_t *enumerator, uint64_t *value_out)
{
    uint64_t value;
    bool is_int;
    char *tmp;

    if (NULL == src || 0 == strlen (src)) {
        if (NULL == enumerator) {
            *value_out = 0;
        }

        return OCOMS_SUCCESS;
    }

    if (enumerator) {
        int int_val, ret;
        ret = enumerator->value_from_string(enumerator, src, &int_val);
        if (OCOMS_SUCCESS != ret) {
            return ret;
        }
        *value_out = (uint64_t) int_val;

        return OCOMS_SUCCESS;
    }

    /* Check for an integer value */
    value = strtoull (src, &tmp, 0);
    is_int = tmp[0] == '\0';

    if (!is_int && tmp != src) {
        switch (tmp[0]) {
        case 'G':
        case 'g':
            value <<= 10;
        case 'M':
        case 'm':
            value <<= 10;
        case 'K':
        case 'k':
            value <<= 10;
            break;
        default:
            break;
        }
    }

    *value_out = value;

    return OCOMS_SUCCESS;
}

static int var_set_from_string (ocoms_mca_base_var_t *var, char *src)
{
    ocoms_mca_base_var_storage_t *dst = var->mbv_storage;
    uint64_t int_value = 0;
    int ret;

    switch (var->mbv_type) {
    case MCA_BASE_VAR_TYPE_INT:
    case MCA_BASE_VAR_TYPE_UNSIGNED_INT:
    case MCA_BASE_VAR_TYPE_UNSIGNED_LONG:
    case MCA_BASE_VAR_TYPE_UNSIGNED_LONG_LONG:
    case MCA_BASE_VAR_TYPE_BOOL:
    case MCA_BASE_VAR_TYPE_SIZE_T:
        ret = int_from_string(src, var->mbv_enumerator, &int_value);
        if (OCOMS_ERR_VALUE_OUT_OF_BOUNDS == ret ||
            (MCA_BASE_VAR_TYPE_INT == var->mbv_type &&
             (sizeof(int) < 8 && (int_value & 0xffffffff00000000ull)))) {
            if (var->mbv_enumerator) {
                char *valid_values;
                (void) var->mbv_enumerator->dump(var->mbv_enumerator, &valid_values);
                /*ocoms_show_help("help-mca-var.txt", "invalid-value-enum",
                               true, var->mbv_full_name, src, valid_values);*/
                fprintf(stderr,"%s:%d: invalid-value-enum: %s:%s:%s\n",
                    __FILE__,__LINE__, var->mbv_full_name, src, valid_values);
                free(valid_values);
            } else {
                /*ocoms_show_help("help-mca-var.txt", "invalid-value",
                               true, var->mbv_full_name, src);*/
                fprintf(stderr,"%s:%d: invalid-value: %s:%s\n",
                    __FILE__,__LINE__, var->mbv_full_name, src);
            }

            return OCOMS_ERR_VALUE_OUT_OF_BOUNDS;
        }

        if (MCA_BASE_VAR_TYPE_INT == var->mbv_type ||
            MCA_BASE_VAR_TYPE_UNSIGNED_INT == var->mbv_type) {
            dst->intval = (int) int_value;
        } else if (MCA_BASE_VAR_TYPE_UNSIGNED_LONG == var->mbv_type) {
            dst->ulval = (unsigned long) int_value;
        } else if (MCA_BASE_VAR_TYPE_UNSIGNED_LONG_LONG == var->mbv_type) {
            dst->ullval = (unsigned long long) int_value;
        } else if (MCA_BASE_VAR_TYPE_SIZE_T == var->mbv_type) {
            dst->sizetval = (size_t) int_value;
        } else if (MCA_BASE_VAR_TYPE_BOOL == var->mbv_type) {
            dst->boolval = !!int_value;
        }

        return ret;
    case MCA_BASE_VAR_TYPE_DOUBLE:
        dst->lfval = strtod (src, NULL);
        break;
    case MCA_BASE_VAR_TYPE_STRING:
        var_set_string (var, src);
        break;
    case MCA_BASE_VAR_TYPE_MAX:
        return OCOMS_ERROR;
    }

    return OCOMS_SUCCESS;
}

/*
 * Set a variable
 */
int ocoms_mca_base_var_set_value (int index, const void *value, size_t size, ocoms_mca_base_var_source_t source,
                            const char *source_file)
{
    ocoms_mca_base_var_t *var;
    int ret;

    ret = var_get (index, &var, true);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    if (!VAR_IS_VALID(var[0])) {
        return OCOMS_ERR_BAD_PARAM;
    }

    if (!VAR_IS_SETTABLE(var[0])) {
        return OCOMS_ERR_PERM;
    }

    if (NULL != var->mbv_enumerator) {
        /* Validate */
        ret = var->mbv_enumerator->string_from_value(var->mbv_enumerator,
                                                     ((int *) value)[0], NULL);
        if (OCOMS_SUCCESS != ret) {
            return ret;
        }
    }

    if (MCA_BASE_VAR_TYPE_STRING != var->mbv_type) {
        memmove (var->mbv_storage, value, var_type_sizes[var->mbv_type]);
    } else {
        var_set_string (var, (char *) value);
    }

    var->mbv_source = source;

    if (MCA_BASE_VAR_SOURCE_FILE == source && NULL != source_file) {
        var->mbv_source_file = append_filename_to_list(source_file);
    }

    return OCOMS_SUCCESS;
}

/*
 * Deregister a parameter
 */
int ocoms_mca_base_var_deregister(int index)
{
    ocoms_mca_base_var_t *var;
    int ret;

    ret = var_get (index, &var, false);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    if (!VAR_IS_VALID(var[0])) {
        return OCOMS_ERR_BAD_PARAM;
    }

    /* Mark this parameter as invalid but keep its info in case this
       parameter is reregistered later */
    var->mbv_flags &= ~MCA_BASE_VAR_FLAG_VALID;

    /* Done deregistering synonym */
    if (MCA_BASE_VAR_FLAG_SYNONYM & var->mbv_flags) {
        return OCOMS_SUCCESS;
    }

    /* Release the current value if it is a string. */
    if (MCA_BASE_VAR_TYPE_STRING == var->mbv_type &&
        var->mbv_storage->stringval) {
        free (var->mbv_storage->stringval);
        var->mbv_storage->stringval = NULL;
    } else if (MCA_BASE_VAR_TYPE_BOOL != var->mbv_type && NULL != var->mbv_enumerator) {
        OBJ_RELEASE(var->mbv_enumerator);
    }

    var->mbv_enumerator = NULL;

    var->mbv_storage = NULL;

    return OCOMS_SUCCESS;
}

static int var_get (int index, ocoms_mca_base_var_t **var_out, bool original)
{
    ocoms_mca_base_var_t *var;

    if (var_out) {
        *var_out = NULL;
    }

    /* Check for bozo cases */    
    if (!ocoms_mca_base_var_initialized) {
        return OCOMS_ERROR;
    }

    if (index < 0) {
        return OCOMS_ERR_BAD_PARAM;
    }

    var = ocoms_pointer_array_get_item (&ocoms_mca_base_vars, index);
    if (NULL == var) {
        return OCOMS_ERR_BAD_PARAM;
    }

    if (VAR_IS_SYNONYM(var[0]) && original) {
        return var_get(var->mbv_synonym_for, var_out, false);
    }

    if (var_out) {
        *var_out = var;
    }

    return OCOMS_SUCCESS;
}

int ocoms_mca_base_var_env_name(const char *param_name,
                          char **env_name)
{
    int ret;

    assert (NULL != env_name);

    ret = asprintf(env_name, "%s%s", mca_prefix, param_name);
    if (0 > ret) {
        return OCOMS_ERR_OUT_OF_RESOURCE;
    }

    return OCOMS_SUCCESS;
}

/*
 * Find the index for an MCA parameter based on its names.
 */
static int var_find_by_name (const char *full_name, int *index, bool invalidok)
{
    ocoms_mca_base_var_t *var;
    void *tmp;
    int rc;

    rc = ocoms_hash_table_get_value_ptr (&ocoms_mca_base_var_index_hash, full_name, strlen (full_name),
                                        &tmp);
    if (OCOMS_SUCCESS != rc) {
        return rc;
    }

    (void) var_get ((int)(uintptr_t) tmp, &var, false);

    if (invalidok || VAR_IS_VALID(var[0])) {
        *index = (int)(uintptr_t) tmp;
        return OCOMS_SUCCESS;
    }

    return OCOMS_ERR_NOT_FOUND;
}

static int var_find (const char *project_name, const char *framework_name,
                     const char *component_name, const char *variable_name,
                     bool invalidok)
{
    char *full_name;
    int ret, index;

    ret = ocoms_mca_base_var_generate_full_name4 (NULL, framework_name, component_name,
                                            variable_name, &full_name);
    if (OCOMS_SUCCESS != ret) {
        return OCOMS_ERROR;
    }

    ret = var_find_by_name(full_name, &index, invalidok);

    /* NTH: should we verify the name components match? */

    free (full_name);
    return (OCOMS_SUCCESS != ret) ? ret : index;
}

/*
 * Find the index for an MCA parameter based on its name components.
 */
int ocoms_mca_base_var_find (const char *project_name, const char *framework_name,
                       const char *component_name, const char *variable_name) 
{
    return var_find (project_name, framework_name, component_name, variable_name, false);
}

/*
 * Find the index for an MCA parameter based on full name.
 */
int ocoms_mca_base_var_find_by_name (const char *full_name, int *index)
{
    return var_find_by_name (full_name, index, false);
}

int ocoms_mca_base_var_set_flag (int index, ocoms_mca_base_var_flag_t flag, bool set)
{
    ocoms_mca_base_var_t *var;
    int ret;

    ret = var_get (index, &var, true);
    if (OCOMS_SUCCESS != ret || VAR_IS_SYNONYM(var[0])) {
        return OCOMS_ERR_BAD_PARAM;
    }

    var->mbv_flags = (var->mbv_flags & ~flag) | (set ? flag : 0);

    /* All done */
    return OCOMS_SUCCESS;
}

/*
 * Return info on a parameter at an index
 */
int ocoms_mca_base_var_get (int index, const ocoms_mca_base_var_t **var)
{
    return var_get (index, (ocoms_mca_base_var_t **) var, false);
}

/*
 * Make an argv-style list of strings suitable for an environment
 */
int ocoms_mca_base_var_build_env(char ***env, int *num_env, bool internal)
{
    ocoms_mca_base_var_t *var;
    size_t i, len;
    int ret;

    /* Check for bozo cases */
    
    if (!ocoms_mca_base_var_initialized) {
        return OCOMS_ERROR;
    }

    /* Iterate through all the registered parameters */

    len = ocoms_pointer_array_get_size(&ocoms_mca_base_vars);
    for (i = 0; i < len; ++i) {
        char *value_string;
        char *str = NULL;

        var = ocoms_pointer_array_get_item (&ocoms_mca_base_vars, i);
        if (NULL == var) {
            continue;
        }

        /* Don't output default values or internal variables (unless
           requested) */
        if (MCA_BASE_VAR_SOURCE_DEFAULT == var->mbv_source ||
            (!internal && VAR_IS_INTERNAL(var[0]))) {
            continue;
        }

        if (MCA_BASE_VAR_TYPE_STRING == var->mbv_type &&
            NULL == var->mbv_storage->stringval) {
            continue;
        }

        ret = var_value_string (var, &value_string);
        if (OCOMS_SUCCESS != ret) {
            goto cleanup;
        }

        ret = asprintf (&str, "%s%s=%s", mca_prefix, var->mbv_full_name,
                        value_string);
        free (value_string);
        if (0 > ret) {
            goto cleanup;
        }

        ocoms_argv_append(num_env, env, str);
        free(str);

        switch (var->mbv_source) {
        case MCA_BASE_VAR_SOURCE_FILE:
        case MCA_BASE_VAR_SOURCE_OVERRIDE:
            asprintf (&str, "%sSOURCE_%s=FILE:%s", mca_prefix, var->mbv_full_name,
                      var->mbv_source_file);
            break;
        case MCA_BASE_VAR_SOURCE_COMMAND_LINE:
            asprintf (&str, "%sSOURCE_%s=COMMAND_LINE", mca_prefix, var->mbv_full_name);
            break;
        case MCA_BASE_VAR_SOURCE_ENV:
        case MCA_BASE_VAR_SOURCE_SET:
        case MCA_BASE_VAR_SOURCE_DEFAULT:
            str = NULL;
            break;
        case MCA_BASE_VAR_SOURCE_MAX:
            goto cleanup;
        }

        if (NULL != str) {
            ocoms_argv_append(num_env, env, str);
            free(str);
        }
    }

    /* All done */

    return OCOMS_SUCCESS;

    /* Error condition */

 cleanup:
    if (*num_env > 0) {
        ocoms_argv_free(*env);
        *num_env = 0;
        *env = NULL;
    }
    return OCOMS_ERR_NOT_FOUND;
}

/*
 * Shut down the MCA parameter system (normally only invoked by the
 * MCA framework itself).
 */
int ocoms_mca_base_var_finalize(void)
{
    ocoms_object_t *object;
    ocoms_list_item_t *item;
    int size, i;

    if (ocoms_mca_base_var_initialized) {
        size = ocoms_pointer_array_get_size(&ocoms_mca_base_vars);
        for (i = 0 ; i < size ; ++i) {
            object = ocoms_pointer_array_get_item (&ocoms_mca_base_vars, i);
            if (NULL != object) {
                OBJ_RELEASE(object);
            }
        }
        OBJ_DESTRUCT(&ocoms_mca_base_vars);

        while (NULL !=
               (item = ocoms_list_remove_first(&ocoms_mca_base_var_file_values))) {
            OBJ_RELEASE(item);
        }
        OBJ_DESTRUCT(&ocoms_mca_base_var_file_values);

        while (NULL !=
               (item = ocoms_list_remove_first(&ocoms_mca_base_var_override_values))) {
            OBJ_RELEASE(item);
        }
        OBJ_DESTRUCT(&ocoms_mca_base_var_override_values);

        if( NULL != cwd ) {
            free(cwd);
            cwd = NULL;
        }

        ocoms_mca_base_var_initialized = false;
        ocoms_mca_base_var_count = 0;

        if (NULL != ocoms_mca_base_var_file_list) {
            ocoms_argv_free(ocoms_mca_base_var_file_list);
        }

        (void) ocoms_mca_base_var_group_finalize ();
        (void) ocoms_mca_base_pvar_finalize ();

        OBJ_DESTRUCT(&ocoms_mca_base_var_index_hash);
    }

    /* All done */

    return OCOMS_SUCCESS;
}


/*************************************************************************/
static int fixup_files(char **file_list, char * path, bool rel_path_search) {
    int exit_status = OCOMS_SUCCESS;
    char **files = NULL;
    char **search_path = NULL;
    char * tmp_file = NULL;
    char **argv = NULL;
    int mode = R_OK; /* The file exists, and we can read it */
    int count, i, argc = 0;

    search_path = ocoms_argv_split(path, OCOMS_ENV_SEP);
    files = ocoms_argv_split(*file_list, OCOMS_ENV_SEP);
    count = ocoms_argv_count(files);

    /* Read in reverse order, so we can preserve the original ordering */
    for (i = 0 ; i < count; ++i) {
        /* Absolute paths preserved */
        if ( ocoms_path_is_absolute(files[i]) ) {
            if( NULL == ocoms_path_access(files[i], NULL, mode) ) {
                /*ocoms_show_help("help-mca-var.txt", "missing-param-file",
                               true, getpid(), files[i], path);*/
                fprintf(stderr,"%s:%d:  missing-param-file: %s:%s\n",
                    __FILE__,__LINE__, files[i], path );
                exit_status = OCOMS_ERROR;
                goto cleanup;
            }
            else {
                ocoms_argv_append(&argc, &argv, files[i]);
            }
        }
        /* Resolve all relative paths:
         *  - If filename contains a "/" (e.g., "./foo" or "foo/bar")
         *    - look for it relative to cwd
         *    - if exists, use it
         *    - ow warn/error
         */
        else if (!rel_path_search && NULL != strchr(files[i], OCOMS_PATH_SEP[0]) ) {
            if( NULL != force_agg_path ) {
                tmp_file = ocoms_path_access(files[i], force_agg_path, mode);
            }
            else {
                tmp_file = ocoms_path_access(files[i], cwd, mode);
            }

            if( NULL == tmp_file ) {
                /*ocoms_show_help("help-mca-var.txt", "missing-param-file",
                               true, getpid(), files[i], cwd);*/
                fprintf(stderr,"%s:%d: missing-param-file: %s:%s\n",
                    __FILE__,__LINE__, files[i], cwd);
                exit_status = OCOMS_ERROR;
                goto cleanup;
            }
            else {
                ocoms_argv_append(&argc, &argv, tmp_file);
            }
        }
        /* Resolve all relative paths:
         * - Use path resolution
         *    - if found and readable, use it
         *    - otherwise, warn/error
         */
        else {
            if( NULL != (tmp_file = ocoms_path_find(files[i], search_path, mode, NULL)) ) {
                ocoms_argv_append(&argc, &argv, tmp_file);
                free(tmp_file);
                tmp_file = NULL;
            }
            else {
                /*ocoms_show_help("help-mca-var.txt", "missing-param-file",
                               true, getpid(), files[i], path);*/
                fprintf(stderr,"%s:%d:  missing-param-file: %s:%s\n",
                    __FILE__,__LINE__, files[i], path );
                exit_status = OCOMS_ERROR;
                goto cleanup;
            }
        }
    }

    free(*file_list);
    *file_list = ocoms_argv_join(argv, OCOMS_ENV_SEP);

 cleanup:
    if( NULL != files ) {
        ocoms_argv_free(files);
        files = NULL;
    }
    if( NULL != argv ) {
        ocoms_argv_free(argv);
        argv = NULL;
    }
    if( NULL != search_path ) {
        ocoms_argv_free(search_path);
        search_path = NULL;
    }
    if( NULL != tmp_file ) {
        free(tmp_file);
        tmp_file = NULL;
    }

    return exit_status;
}

static int read_files(char *file_list, ocoms_list_t *file_values)
{
    int i, count;

    /* Iterate through all the files passed in -- read them in reverse
       order so that we preserve unix/shell path-like semantics (i.e.,
       the entries farthest to the left get precedence) */

    ocoms_mca_base_var_file_list = ocoms_argv_split(file_list, OCOMS_ENV_SEP);
    count = ocoms_argv_count(ocoms_mca_base_var_file_list);

    for (i = count - 1; i >= 0; --i) {
        ocoms_mca_base_parse_paramfile(ocoms_mca_base_var_file_list[i], file_values);
    }

    return OCOMS_SUCCESS;
}

/******************************************************************************/
static int register_variable (const char *project_name, const char *framework_name,
                              const char *component_name, const char *variable_name,
                              const char *description, ocoms_mca_base_var_type_t type,
                              ocoms_mca_base_var_enum_t *enumerator, int bind,
                              ocoms_mca_base_var_flag_t flags, ocoms_mca_base_var_info_lvl_t info_lvl,
                              ocoms_mca_base_var_scope_t scope, int synonym_for,
                              void *storage)
{
    int ret, var_index, group_index;
    ocoms_mca_base_var_group_t *group;
    ocoms_mca_base_var_t *var;

    /* Developer error. Storage can not be NULL and type must exist */
    assert (((flags & MCA_BASE_VAR_FLAG_SYNONYM) || NULL != storage) && type >= 0 && type < MCA_BASE_VAR_TYPE_MAX);

    /* There are data holes in the var struct */
    OCOMS_DEBUG_ZERO(var);

    /* Initialize the array if it has never been initialized */
    if (!ocoms_mca_base_var_initialized) {
        ocoms_mca_base_var_init();
    }

    /* XXX -- readd project name once it is available in the component structure */
    project_name = NULL;

    /* See if this entry is already in the array */
#if 0
    var_index = var_find (project_name, framework_name, component_name, variable_name,
                          true);
#else
    /* Warning: only variable name is used for checking the uniqueness of variable */
    var_index = var_find (NULL, NULL, NULL, variable_name, true);
#endif

    if (0 > var_index) {
        /* Create a new parameter entry */
        group_index = ocoms_mca_base_var_group_register (project_name, framework_name, component_name,
                                                   NULL);
        if (-1 > group_index) {
            return group_index;
        }

        /* Read-only and constant variables can't be settable */
        if (scope < MCA_BASE_VAR_SCOPE_LOCAL || (flags & MCA_BASE_VAR_FLAG_DEFAULT_ONLY)) {
            if ((flags & MCA_BASE_VAR_FLAG_DEFAULT_ONLY) && (flags & MCA_BASE_VAR_FLAG_SETTABLE)) {
                /*ocoms_show_help("help-mca-var.txt", "invalid-flag-combination",
                               true, "MCA_BASE_VAR_FLAG_DEFAULT_ONLY", "MCA_BASE_VAR_FLAG_SETTABLE");*/
                fprintf(stderr,"%s:%d: invalid-flag-combination: %s:%s\n",
                    __FILE__,__LINE__, "MCA_BASE_VAR_FLAG_DEFAULT_ONLY", "MCA_BASE_VAR_FLAG_SETTABLE" );
                return OCOMS_ERROR;
            }

            /* Should we print a warning for other cases? */
            flags &= ~MCA_BASE_VAR_FLAG_SETTABLE;
        }

        var = OBJ_NEW(ocoms_mca_base_var_t);

        var->mbv_type        = type;
        var->mbv_flags       = flags;
        var->mbv_group_index = group_index;
        var->mbv_info_lvl  = info_lvl;
        var->mbv_scope       = scope;
        var->mbv_synonym_for = synonym_for;
        var->mbv_bind        = bind;

        if (NULL != description) {
            var->mbv_description = strdup(description);
        }

        if (NULL != variable_name) {
            var->mbv_variable_name = strdup(variable_name);
            if (NULL == var->mbv_variable_name) {
                OBJ_RELEASE(var);
                return OCOMS_ERR_OUT_OF_RESOURCE;
            }
        }

        ret = ocoms_mca_base_var_generate_full_name4 (NULL, NULL, NULL,
                                                variable_name, &var->mbv_full_name);
        if (OCOMS_SUCCESS != ret) {
            OBJ_RELEASE(var);
            return OCOMS_ERROR;
        }

        ret = ocoms_mca_base_var_generate_full_name4 (project_name, framework_name, component_name,
                                                variable_name, &var->mbv_long_name);
        if (OCOMS_SUCCESS != ret) {
            OBJ_RELEASE(var);
            return OCOMS_ERROR;
        }

        /* Add it to the array.  Note that we copy the mca_var_t by value,
           so the entire contents of the struct is copied.  The synonym list
           will always be empty at this point, so there's no need for an
           extra RETAIN or RELEASE. */
        var_index = ocoms_pointer_array_add (&ocoms_mca_base_vars, var);
        if (0 > var_index) {
            OBJ_RELEASE(var);
            return OCOMS_ERROR;
        }

        var->mbv_index = var_index;

        if (0 <= group_index) {
            ocoms_mca_base_var_group_add_var (group_index, var_index);
        }

        ocoms_mca_base_var_count++;
        ocoms_hash_table_set_value_ptr (&ocoms_mca_base_var_index_hash, var->mbv_full_name, strlen (var->mbv_full_name),
                                       (void *)(uintptr_t) var_index);
    } else {
        ret = var_get (var_index, &var, false);
        if (OCOMS_SUCCESS != ret) {
            /* Shouldn't ever happen */
            return OCOMS_ERROR;
        }

        ret = ocoms_mca_base_var_group_get_internal (var->mbv_group_index, &group, true);
        if (OCOMS_SUCCESS != ret) {
            /* Shouldn't ever happen */
            return OCOMS_ERROR;
        }
#if 0
        /* Verify the name components match */
        if (0 != compare_strings(framework_name, group->group_framework) ||
            0 != compare_strings(component_name, group->group_component) ||
            0 != compare_strings(variable_name, var->mbv_variable_name)) {
            /*ocoms_show_help("help-mca-var.txt", "var-name-conflict",
                           true, var->mbv_full_name, framework_name,
                           component_name, variable_name,
                           group->group_framework, group->group_component,
                           var->mbv_variable_name);*/
            fprintf(stderr,"%s:%d: var-name-conflict: %s:%s\n",
                __FILE__,__LINE__);
            /* This is developer error. abort! */
            assert (0);
            return OCOMS_ERROR;
        }
#endif
        if (var->mbv_type != type) {
#if OCOMS_ENABLE_DEBUG
            /*ocoms_show_help("help-mca-var.txt",
                           "re-register-with-different-type",
                           true, var->mbv_full_name);*/
            fprintf(stderr,"%s:%d: re-register-with-different-type: %s\n",
                __FILE__,__LINE__, var->mbv_full_name );
#endif
            return OCOMS_ERR_VALUE_OUT_OF_BOUNDS;
        }
    }

    if (MCA_BASE_VAR_TYPE_BOOL == var->mbv_type) {
        enumerator = &ocoms_mca_base_var_enum_bool;
    } else if (NULL != enumerator) {
        if (var->mbv_enumerator) {
            OBJ_RELEASE (var->mbv_enumerator);
        }

        OBJ_RETAIN(enumerator);
    }

    var->mbv_enumerator = enumerator;

    if (flags & MCA_BASE_VAR_FLAG_SYNONYM) {
        ocoms_mca_base_var_t *original = ocoms_pointer_array_get_item (&ocoms_mca_base_vars, synonym_for);

        ocoms_value_array_append_item(&original->mbv_synonyms, &var_index);
    } else {
        var->mbv_storage = storage;

        /* make a copy of the default string value */
        if (MCA_BASE_VAR_TYPE_STRING == type && NULL != ((char **)storage)[0]) {
            ((char **)storage)[0] = strdup (((char **)storage)[0]);
        }
    }

    ret = var_set_initial (var);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    var->mbv_flags |= MCA_BASE_VAR_FLAG_VALID;

    /* All done */
    return var_index;
}

int ocoms_mca_base_var_register (const char *project_name, const char *framework_name,
                           const char *component_name, const char *variable_name,
                           const char *description, ocoms_mca_base_var_type_t type,
                           ocoms_mca_base_var_enum_t *enumerator, int bind,
                           ocoms_mca_base_var_flag_t flags,
                           ocoms_mca_base_var_info_lvl_t info_lvl,
                           ocoms_mca_base_var_scope_t scope, void *storage)
{
    /* Only integer variables can have enumerator */
    assert (NULL == enumerator || MCA_BASE_VAR_TYPE_INT == type);

    return register_variable (project_name, framework_name, component_name,
                              variable_name, description, type, enumerator,
                              bind, flags, info_lvl, scope, -1, storage);
}

int ocoms_mca_base_component_var_register (const ocoms_mca_base_component_t *component,
                                     const char *variable_name, const char *description,
                                     ocoms_mca_base_var_type_t type, ocoms_mca_base_var_enum_t *enumerator,
                                     int bind, ocoms_mca_base_var_flag_t flags,
                                     ocoms_mca_base_var_info_lvl_t info_lvl,
                                     ocoms_mca_base_var_scope_t scope, void *storage)
{
    /* XXX -- component_update -- We will stash the project name in the component */
    return ocoms_mca_base_var_register (NULL, component->mca_type_name,
                                  component->mca_component_name,
                                  variable_name, description, type, enumerator,
                                  bind, flags | MCA_BASE_VAR_FLAG_DWG,
                                  info_lvl, scope, storage);
}

int ocoms_mca_base_framework_var_register (const ocoms_mca_base_framework_t *framework,
                                     const char *variable_name,
                                     const char *help_msg, ocoms_mca_base_var_type_t type,
                                     ocoms_mca_base_var_enum_t *enumerator, int bind,
                                     ocoms_mca_base_var_flag_t flags,
                                     ocoms_mca_base_var_info_lvl_t info_level,
                                     ocoms_mca_base_var_scope_t scope, void *storage)
{
    return ocoms_mca_base_var_register (framework->framework_project, framework->framework_name,
                                  "base", variable_name, help_msg, type, enumerator, bind,
                                  flags | MCA_BASE_VAR_FLAG_DWG, info_level, scope, storage);
}

int ocoms_mca_base_var_register_synonym (int synonym_for, const char *project_name,
                                   const char *framework_name,
                                   const char *component_name,
                                   const char *synonym_name,
                                   ocoms_mca_base_var_syn_flag_t flags)
{
    ocoms_mca_base_var_flag_t var_flags = MCA_BASE_VAR_FLAG_SYNONYM;
    ocoms_mca_base_var_t *var;
    int ret;

    ret = var_get (synonym_for, &var, false);
    if (OCOMS_SUCCESS != ret || VAR_IS_SYNONYM(var[0])) {
        return OCOMS_ERR_BAD_PARAM;
    }

    if (flags & MCA_BASE_VAR_SYN_FLAG_DEPRECATED) {
        var_flags |= MCA_BASE_VAR_FLAG_DEPRECATED;
    }
    if (flags & MCA_BASE_VAR_SYN_FLAG_INTERNAL) {
        var_flags |= MCA_BASE_VAR_FLAG_INTERNAL;
    }

    return register_variable (project_name, framework_name, component_name,
                              synonym_name, var->mbv_description, var->mbv_type, var->mbv_enumerator,
                              var->mbv_bind, var_flags, var->mbv_info_lvl, var->mbv_scope,
                              synonym_for, NULL);
}

/*
 * Lookup a param in the environment
 */
static int var_set_from_env (ocoms_mca_base_var_t *var)
{
    const char *var_full_name = var->mbv_full_name;
    bool deprecated = VAR_IS_DEPRECATED(var[0]);
    char *source, *source_env;
    char *value, *value_env;
    int ret;
    
    if (VAR_IS_SYNONYM(var[0])) {
        ret = var_get (var->mbv_synonym_for, &var, true);
        if (OCOMS_SUCCESS != ret) {
            return OCOMS_ERROR;
        }

        if (var->mbv_source >= MCA_BASE_VAR_SOURCE_ENV) {
            return OCOMS_SUCCESS;
        }
    }

    ret = asprintf (&source, "%sSOURCE_%s", mca_prefix, var_full_name);
    if (0 > ret) {
        return OCOMS_ERROR;
    }

    ret = asprintf (&value, "%s%s", mca_prefix, var_full_name);
    if (0 > ret) {
        free (source);
        return OCOMS_ERROR;
    }

    source_env = getenv (source);
    value_env = getenv (value);

    free (source);
    free (value);

    if (NULL == value_env) {
        return OCOMS_ERR_NOT_FOUND;
    }

    /* we found an environment variable but this variable is default-only. print
       a warning. */
    if (VAR_IS_DEFAULT_ONLY(var[0])) {
       /* ocoms_show_help("help-mca-var.txt", "default-only-param-set",
                true, var_full_name);*/
        fprintf(stderr,"%s:%d: default-only-param-set: %s\n",
            __FILE__,__LINE__, var_full_name);

        return OCOMS_ERR_NOT_FOUND;
    }

    if (MCA_BASE_VAR_SOURCE_OVERRIDE == var->mbv_source) {
        if (!ocoms_mca_base_var_suppress_override_warning) {
            /*ocoms_show_help("help-mca-var.txt", "overridden-param-set",
                    true, var_full_name);*/
            fprintf(stderr,"%s:%d: overridden-param-set: %s\n",
                    __FILE__,__LINE__, var_full_name);
        }

        return OCOMS_ERR_NOT_FOUND;
    }

    var->mbv_source = MCA_BASE_VAR_SOURCE_ENV;

    if (NULL != source_env) {
        if (0 == strncasecmp (source_env, "file:", 5)) {
            var->mbv_source_file = append_filename_to_list(source_env + 5);
            if (0 == strcmp (var->mbv_source_file, ocoms_mca_base_var_override_file)) {
                var->mbv_source = MCA_BASE_VAR_SOURCE_OVERRIDE;
            } else {
                var->mbv_source = MCA_BASE_VAR_SOURCE_FILE;
            }
        } else if (0 == strcasecmp (source_env, "command")) {
            var->mbv_source = MCA_BASE_VAR_SOURCE_COMMAND_LINE;
        }
    }

    if (deprecated) {
        switch (var->mbv_source) {
        case MCA_BASE_VAR_SOURCE_ENV:
            /*ocoms_show_help("help-mca-var.txt", "deprecated-mca-env",
              true, var->mbv_full_name);*/
            fprintf(stderr,"%s:%d: deprecated-mca-env %s:%s\n",
                    __FILE__,__LINE__, var->mbv_full_name);
            break;
        case MCA_BASE_VAR_SOURCE_COMMAND_LINE:
            /*ocoms_show_help("help-mca-var.txt", "deprecated-mca-cli",
              true, var->mbv_full_name);*/
            fprintf(stderr,"%s:%d: deprecated-mca-cli  %s\n",
                    __FILE__,__LINE__, var->mbv_full_name );
            break;
        case MCA_BASE_VAR_SOURCE_FILE:
        case MCA_BASE_VAR_SOURCE_OVERRIDE:
            /*ocoms_show_help("help-mca-var.txt", "deprecated-mca-file",
                    true, var->mbv_full_name, var->mbv_source_file);*/
            fprintf(stderr,"%s:%d: deprecated-mca-file: %s:%s\n",
                    __FILE__,__LINE__, var->mbv_full_name, var->mbv_source_file);
            break;
 
        case MCA_BASE_VAR_SOURCE_DEFAULT:
        case MCA_BASE_VAR_SOURCE_MAX:
        case MCA_BASE_VAR_SOURCE_SET:
            /* silence compiler warnings about unhandled enumerations */
            break;
        }
    }

    return var_set_from_string (var, value_env);
}


/*
 * Lookup a param in the files
 */
static int var_set_from_file (ocoms_mca_base_var_t *var, ocoms_list_t *file_values)
{
    const char *var_full_name = var->mbv_full_name;
    const char *var_long_name = var->mbv_long_name;
    bool deprecated = VAR_IS_DEPRECATED(var[0]);
    ocoms_mca_base_var_file_value_t *fv;
    int ret;

    if (VAR_IS_SYNONYM(var[0])) {
        ret = var_get (var->mbv_synonym_for, &var, true);
        if (OCOMS_SUCCESS != ret) {
            return OCOMS_ERROR;
        }

        if (var->mbv_source >= MCA_BASE_VAR_SOURCE_FILE) {
            return OCOMS_SUCCESS;
        }
    }

    /* Scan through the list of values read in from files and try to
       find a match.  If we do, cache it on the param (for future
       lookups) and save it in the storage. */

    OCOMS_LIST_FOREACH(fv, file_values, ocoms_mca_base_var_file_value_t) {
        if (0 != strcmp(fv->mbvfv_var, var_full_name) &&
            0 != strcmp(fv->mbvfv_var, var_long_name)) {
            continue;
        }

        /* found it */
        if (VAR_IS_DEFAULT_ONLY(var[0])) {
            /*ocoms_show_help("help-mca-var.txt", "default-only-param-set",
              true, var_full_name);*/
            fprintf(stderr,"%s:%d: default-only-param-set: %s\n",
                    __FILE__,__LINE__, var_full_name);

            return OCOMS_ERR_NOT_FOUND;
        }

        if (MCA_BASE_VAR_FLAG_ENVIRONMENT_ONLY & var->mbv_flags) {
            /*ocoms_show_help("help-mca-var.txt", "environment-only-param",
              true, var_full_name, fv->mbvfv_value,
              fv->mbvfv_file);*/
            fprintf(stderr,"%s:%d: environment-only-param %s:%s:%s\n",
                    __FILE__,__LINE__, var_full_name, fv->mbvfv_value, fv->mbvfv_file);

            return OCOMS_ERR_NOT_FOUND;
        }

        if (MCA_BASE_VAR_SOURCE_OVERRIDE == var->mbv_source) {
            if (!ocoms_mca_base_var_suppress_override_warning) {
                /*ocoms_show_help("help-mca-var.txt", "overridden-param-set",
                  true, var_full_name);*/
                fprintf(stderr,"%s:%d: overridden-param-set: %s\n",
                        __FILE__,__LINE__, var_full_name);
            }

            return OCOMS_ERR_NOT_FOUND;
        }

        if (deprecated) {
            /*ocoms_show_help("help-mca-var.txt", "deprecated-mca-file",
              true, var_full_name, fv->mbvfv_file);*/
            fprintf(stderr,"%s:%d: deprecated-mca-file: %s:%s\n",
                    __FILE__,__LINE__, var_full_name, fv->mbvfv_file);
        }

        if (NULL != fv->mbvfv_file) {
            var->mbv_source_file = fv->mbvfv_file;
            if (NULL == var->mbv_source_file) {
                return OCOMS_ERR_OUT_OF_RESOURCE;
            }
        }

        var->mbv_source = MCA_BASE_VAR_SOURCE_FILE;

        return var_set_from_string (var, fv->mbvfv_value);
    }

    return OCOMS_ERR_NOT_FOUND;
}

/*
 * Lookup the initial value for a parameter
 */
static int var_set_initial (ocoms_mca_base_var_t *var)
{
    int ret;

    var->mbv_source = MCA_BASE_VAR_SOURCE_DEFAULT;

    /* Check all the places that the param may be hiding, in priority
       order. If the default only flag is set the user will get a
       warning if they try to set a value from the environment or a
       file. */
    ret = var_set_from_file (var, &ocoms_mca_base_var_override_values);
    if (OCOMS_SUCCESS == ret) {
        var->mbv_flags = ~MCA_BASE_VAR_FLAG_SETTABLE & (var->mbv_flags | MCA_BASE_VAR_FLAG_OVERRIDE);
        var->mbv_source = MCA_BASE_VAR_SOURCE_OVERRIDE;
    }

    ret = var_set_from_env (var);
    if (OCOMS_ERR_NOT_FOUND != ret) {
        return ret;
    }

    ret = var_set_from_file (var, &ocoms_mca_base_var_file_values);
    if (OCOMS_ERR_NOT_FOUND != ret) {
        return ret;
    }

    return OCOMS_SUCCESS;
}

/*
 * Create an empty param container
 */
static void var_constructor(ocoms_mca_base_var_t *var)
{
    memset ((char *) var + sizeof (var->super), 0, sizeof (*var) - sizeof (var->super));

    var->mbv_type = MCA_BASE_VAR_TYPE_MAX;
    OBJ_CONSTRUCT(&var->mbv_synonyms, ocoms_value_array_t);
    ocoms_value_array_init (&var->mbv_synonyms, sizeof (int));
}


/*
 * Free all the contents of a param container
 */
static void var_destructor(ocoms_mca_base_var_t *var)
{
    if (NULL != var->mbv_variable_name) {
        free(var->mbv_variable_name);
    }
    if (NULL != var->mbv_full_name) {
        free(var->mbv_full_name);
    }
    if (NULL != var->mbv_long_name) {
        free(var->mbv_long_name);
    }
    if (NULL != var->mbv_description) {
        free(var->mbv_description);
    }
    if (MCA_BASE_VAR_TYPE_STRING == var->mbv_type &&
        NULL != var->mbv_storage &&
        NULL != var->mbv_storage->stringval) {
        free (var->mbv_storage->stringval);
    }

    /* don't release the boolean enumerator */
    if (MCA_BASE_VAR_TYPE_BOOL != var->mbv_type && NULL != var->mbv_enumerator) {
        OBJ_RELEASE(var->mbv_enumerator);
    }

    /* Destroy the synonym array */
    OBJ_DESTRUCT(&var->mbv_synonyms);

    /* mark this parameter as invalid */
    var->mbv_type = MCA_BASE_VAR_TYPE_MAX;

#if OCOMS_ENABLE_DEBUG
    /* Cheap trick to reset everything to NULL */
    memset ((char *) var + sizeof (var->super), 0, sizeof (*var) - sizeof (var->super));
#endif
}


static void fv_constructor(ocoms_mca_base_var_file_value_t *f)
{
    memset ((char *) f + sizeof (f->super), 0, sizeof (*f) - sizeof (f->super));
}


static void fv_destructor(ocoms_mca_base_var_file_value_t *f)
{
    if (NULL != f->mbvfv_var) {
        free(f->mbvfv_var);
    }
    if (NULL != f->mbvfv_value) {
        free(f->mbvfv_value);
    }
    /* the file name is stored in mca_*/
    fv_constructor(f);
}

static char *source_name(ocoms_mca_base_var_t *var)
{
    char *ret;

    if (MCA_BASE_VAR_SOURCE_FILE == var->mbv_source || MCA_BASE_VAR_SOURCE_OVERRIDE == var->mbv_source) {
        int rc = asprintf(&ret, "file (%s)", var->mbv_source_file);
        /* some compilers will warn if the return code of asprintf is not checked (even if it is cast to void) */
        if (0 > rc) {
            return NULL;
        }
        return ret;
    } else if (MCA_BASE_VAR_SOURCE_MAX <= var->mbv_source) {
        return strdup ("unknown(!!)");
    }

    return strdup (var_source_names[var->mbv_source]);
}

static int var_value_string (ocoms_mca_base_var_t *var, char **value_string)
{
    const ocoms_mca_base_var_storage_t *value;
    const char *tmp;
    int ret;

    assert (MCA_BASE_VAR_TYPE_MAX > var->mbv_type);

    ret = ocoms_mca_base_var_get_value(var->mbv_index, &value, NULL, NULL);
    if (OCOMS_SUCCESS !=ret) {
        return ret;
    }

    if (NULL == var->mbv_enumerator) {
        if (MCA_BASE_VAR_TYPE_STRING == var->mbv_type) {
            ret = asprintf (value_string, "%s", value->stringval ? value->stringval : "");
        } else {
            ret = asprintf (value_string, var_type_formats[var->mbv_type], value[0]);
        }

        ret = (0 > ret) ? OCOMS_ERR_OUT_OF_RESOURCE : OCOMS_SUCCESS;
    } else {
        ret = var->mbv_enumerator->string_from_value(var->mbv_enumerator, value->intval, &tmp);

        *value_string = strdup (tmp);
        if (NULL == value_string) {
            ret = OCOMS_ERR_OUT_OF_RESOURCE;
        }
    }

    return ret;
}

int ocoms_mca_base_var_check_exclusive (const char *project,
                                  const char *type_a,
                                  const char *component_a,
                                  const char *param_a,
                                  const char *type_b,
                                  const char *component_b,
                                  const char *param_b)
{
    ocoms_mca_base_var_t *var_a, *var_b;
    int var_ai, var_bi;

    /* XXX -- Remove me once the project name is in the componennt */
    project = NULL;

    var_ai = ocoms_mca_base_var_find (project, type_a, component_a, param_a);
    if (var_ai < 0) {
        return OCOMS_ERR_NOT_FOUND;
    }

    var_bi = ocoms_mca_base_var_find (project, type_b, component_b, param_b);
    if (var_bi < 0) {
        return OCOMS_ERR_NOT_FOUND;
    }

    (void) var_get (var_ai, &var_a, true);
    (void) var_get (var_bi, &var_b, true);

    if (MCA_BASE_VAR_SOURCE_DEFAULT != var_a->mbv_source &&
        MCA_BASE_VAR_SOURCE_DEFAULT != var_b->mbv_source) {
        char *str_a, *str_b;

        /* Form cosmetic string names for A */
        str_a = source_name(var_a);

        /* Form cosmetic string names for B */
        str_b = source_name(var_b);

        /* Print it all out */
        /*ocoms_show_help("help-mca-var.txt", 
                       "mutually-exclusive-vars",
                       true, var_a->mbv_full_name,
                       str_a, var_b->mbv_full_name,
                       str_b);*/
        fprintf(stderr,"%s:%d: mutually-exclusive-vars\n",
            __FILE__,__LINE__);

        /* Free the temp strings */
        free(str_a);
        free(str_b);

        return OCOMS_ERR_BAD_PARAM;
    }

    return OCOMS_SUCCESS;
}

int ocoms_mca_base_var_get_count (void)
{
    return ocoms_mca_base_var_count;
}

int ocoms_mca_base_var_dump(int index, char ***out, ocoms_mca_base_var_dump_type_t output_type)
{
    const char *framework, *component, *full_name;
    int i, line_count, line = 0, enum_count = 0;
    char *value_string, *source_string, *tmp;
    int synonym_count, ret, *synonyms = NULL;
    ocoms_mca_base_var_t *var, *original=NULL;
    ocoms_mca_base_var_group_t *group;

    ret = var_get(index, &var, false);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    ret = ocoms_mca_base_var_group_get_internal(var->mbv_group_index, &group, false);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    if (VAR_IS_SYNONYM(var[0])) {
        ret = var_get(var->mbv_synonym_for, &original, false);
        if (OCOMS_SUCCESS != ret) {
            return ret;
        }
        /* just for protection... */
        if (NULL == original) {
            return OCOMS_ERR_NOT_FOUND;
        }
    }

    framework = group->group_framework;
    component = group->group_component ? group->group_component : "base";
    full_name = var->mbv_full_name;

    synonym_count = ocoms_value_array_get_size(&var->mbv_synonyms);
    if (synonym_count) {
        synonyms = OCOMS_VALUE_ARRAY_GET_BASE(&var->mbv_synonyms, int);
    }

    ret = var_value_string (var, &value_string);
    if (OCOMS_SUCCESS != ret) {
        return ret;
    }

    source_string = source_name(var);

    if (MCA_BASE_VAR_DUMP_PARSABLE == output_type) {
        if (NULL != var->mbv_enumerator) {
            (void) var->mbv_enumerator->get_count(var->mbv_enumerator, &enum_count);
        }

        line_count = 8 + (var->mbv_description ? 1 : 0) + (VAR_IS_SYNONYM(var[0]) ? 1 : synonym_count) +
            enum_count;

        *out = (char **) calloc (line_count + 1, sizeof (char *));
        if (NULL == *out) {
            free (value_string);
            free (source_string);
            return OCOMS_ERR_OUT_OF_RESOURCE;
        }

        /* build the message*/
        asprintf(&tmp, "mca:%s:%s:param:%s:", framework, component,
                 full_name);

        /* Output the value */
        asprintf(out[0] + line++, "%svalue:%s", tmp, value_string);
        free(value_string);

        /* Output the source */
        asprintf(out[0] + line++, "%ssource:%s", tmp, source_string);
        free(source_string);

        /* Output whether it's read only or writable */
        asprintf(out[0] + line++, "%sstatus:%s", tmp, VAR_IS_DEFAULT_ONLY(var[0]) ? "read-only" : "writeable");

        /* Output the info level of this parametere */
        asprintf(out[0] + line++, "%slevel:%d", tmp, var->mbv_info_lvl + 1);

        /* If it has a help message, output the help message */
        if (var->mbv_description) {
            asprintf(out[0] + line++, "%shelp:%s", tmp, var->mbv_description);
        }

        if (NULL != var->mbv_enumerator) {
            for (i = 0 ; i < enum_count ; ++i) {
                const char *enum_string = NULL;
                int enum_value;

                ret = var->mbv_enumerator->get_value(var->mbv_enumerator, i, &enum_value,
                                                     &enum_string);
                if (OCOMS_SUCCESS != ret) {
                    continue;
                }

                asprintf(out[0] + line++, "%senumerator:value:%d:%s", tmp, enum_value, enum_string);
            }
        }

        /* Is this variable deprecated? */
        asprintf(out[0] + line++, "%sdeprecated:%s", tmp, VAR_IS_DEPRECATED(var[0]) ? "yes" : "no");

        asprintf(out[0] + line++, "%stype:%s", tmp, var_type_names[var->mbv_type]);

        /* Does this parameter have any synonyms or is it a synonym? */
        if (VAR_IS_SYNONYM(var[0])) {
            asprintf(out[0] + line++, "%ssynonym_of:name:%s", tmp, original->mbv_full_name);
        } else if (ocoms_value_array_get_size(&var->mbv_synonyms)) {
            for (i = 0 ; i < synonym_count ; ++i) {
                ocoms_mca_base_var_t *synonym;

                ret = var_get(synonyms[i], &synonym, false);
                if (OCOMS_SUCCESS != ret) {
                    continue;
                }

                asprintf(out[0] + line++, "%ssynonym:name:%s", tmp, synonym->mbv_full_name);
            }
        }

        free (tmp);
    } else if (MCA_BASE_VAR_DUMP_READABLE == output_type) {
        /* There will be at most three lines in the pretty print case */
        *out = (char **) calloc (4, sizeof (char *));
        if (NULL == *out) {
            free (value_string);
            free (source_string);
            return OCOMS_ERR_OUT_OF_RESOURCE;
        }

//        asprintf (out[0], "%s \"%s\" (current value: \"%s\", data source: %s, level: %d %s, type: %s",
//                  VAR_IS_DEFAULT_ONLY(var[0]) ? "informational" : "parameter",
//                  full_name, value_string, source_string, var->mbv_info_lvl + 1,
//                  info_lvl_strings[var->mbv_info_lvl], var_type_names[var->mbv_type]);
        asprintf (out[0], "%s \"%s\" (default value: \"%s\", type: %s",
                  VAR_IS_DEFAULT_ONLY(var[0]) ? "informational" : "parameter",
                  full_name, value_string, var_type_names[var->mbv_type]);
        free (value_string);
        free (source_string);

        tmp = out[0][0];
        if (VAR_IS_DEPRECATED(var[0])) {
            asprintf (out[0], "%s, deprecated", tmp);
            free (tmp);
            tmp = out[0][0];
        }

        /* Does this parameter have any synonyms or is it a synonym? */
        if (VAR_IS_SYNONYM(var[0])) {
            asprintf(out[0], "%s, synonym of: %s)", tmp, original->mbv_full_name);
            free (tmp);
        } else if (synonym_count) {
            asprintf(out[0], "%s, synonyms: ", tmp);
            free (tmp);

            for (i = 0 ; i < synonym_count ; ++i) {
                ocoms_mca_base_var_t *synonym;

                ret = var_get(synonyms[i], &synonym, false);
                if (OCOMS_SUCCESS != ret) {
                    continue;
                }

                tmp = out[0][0];
                if (synonym_count == i+1) {
                    asprintf(out[0], "%s%s)", tmp, synonym->mbv_full_name);
                } else {
                    asprintf(out[0], "%s%s, ", tmp, synonym->mbv_full_name);
                }
                free(tmp);
            }
        } else {
            asprintf(out[0], "%s)", tmp);
            free(tmp);
        }

        line++;

        if (var->mbv_description) {
            asprintf(out[0] + line++, "%s", var->mbv_description);
        }

        if (NULL != var->mbv_enumerator) {
            char *values;

            ret = var->mbv_enumerator->dump(var->mbv_enumerator, &values);
            if (OCOMS_SUCCESS == ret) {
                asprintf (out[0] + line++, "Valid values: %s", values);
                free (values);
            }
        }
    } else if (MCA_BASE_VAR_DUMP_SIMPLE == output_type) {
        *out = (char **) calloc (2, sizeof (char *));
        if (NULL == *out) {
            free (value_string);
            free (source_string);
            return OCOMS_ERR_OUT_OF_RESOURCE;
        }

        asprintf(out[0], "%s=%s (%s)", var->mbv_full_name, value_string, source_string);

        free (value_string);
        free (source_string);
    }

    return OCOMS_SUCCESS;
}
