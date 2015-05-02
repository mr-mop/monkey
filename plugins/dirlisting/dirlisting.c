/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Monkey HTTP Server
 *  ==================
 *  Copyright 2001-2015 Monkey Software LLC <eduardo@monkey.io>
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Some history about this module
 * ------------------------------
 * 2008 - Rewrite module, suport dynamic themes by Eduardo Silva
 * 2008 - Felipe Astroza (max) provided the mk_dirhtml_human_readable_size_func()
 * 2007 - Add struct client_request support by Eduardo
 * 2002 - Original version written by Daniel R. Ome
 */

#include <monkey/mk_api.h>
#include "dirlisting.h"

#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

const mk_ptr_t mk_dirhtml_default_mime = mk_ptr_init(MK_DIRHTML_DEFAULT_MIME);
const mk_ptr_t mk_dir_iov_dash  = mk_ptr_init("-");
const mk_ptr_t mk_dir_iov_none  = mk_ptr_init("");
const mk_ptr_t mk_dir_iov_slash = mk_ptr_init("/");

/* Function wrote by Max (Felipe Astroza), thanks! */
static char *mk_dirhtml_human_readable_size(off_t size)
{
    unsigned long u = 1024, i, len;
    char *buf = NULL;
    static const char *__units[] = {
        "b", "K", "M", "G",
        "T", "P", "E", "Z", "Y", NULL
    };

    for (i = 0; __units[i] != NULL; i++) {
        if ((size / u) == 0) {
            break;
        }
        u *= 1024;
    }
    if (!i) {
        mk_api->str_build(&buf, &len, "%lu%s", (long unsigned int) size, __units[0]);
    }
    else {
        float fsize = (float) ((double) size / (u / 1024));
        mk_api->str_build(&buf, &len, "%.1f%s", fsize, __units[i]);
    }

    return buf;
}

static struct mk_f_list *mk_dirhtml_create_element(char *file,
                                            unsigned char type,
                                            char *full_path,
                                            unsigned long *list_len)
{
    int n;
    struct tm *st_time;
    struct mk_f_list *entry;

    entry = mk_api->mem_alloc_z(sizeof(struct mk_f_list));

    if (mk_api->file_get_info(full_path, &entry->info, MK_FILE_READ) != 0) {
        mk_api->mem_free(entry);
        return NULL;
    }

    strcpy(entry->name, file);
    entry->type = type;
    entry->next = NULL;

    st_time = localtime((time_t *) & entry->info.last_modification);
    n = strftime(entry->ft_modif, MK_DIRHTML_FMOD_LEN, "%d-%b-%G %H:%M", st_time);
    if (n == 0) {
        mk_mem_free(entry);
        return NULL;
    }

    if (type != DT_DIR) {
        entry->size = mk_dirhtml_human_readable_size(entry->info.size);
    }
    else {
        entry->size = MK_DIRHTML_SIZE_DIR;
    }

    *list_len = *list_len + 1;

    return entry;
}

static struct mk_f_list *mk_dirhtml_create_list(DIR * dir, char *path,
                                                unsigned long *list_len)
{
    unsigned long len;
    char *full_path = NULL;
    struct dirent *ent;
    struct mk_f_list *list = 0, *entry = 0, *last = 0;

    while ((ent = readdir(dir)) != NULL) {
        if ((ent->d_name[0] == '.') && (strcmp(ent->d_name, "..") != 0))
            continue;

        /* Look just for files and dirs */
        if (ent->d_type != DT_REG && ent->d_type != DT_DIR
            && ent->d_type != DT_LNK && ent->d_type != DT_UNKNOWN) {
            continue;
        }

        mk_api->str_build(&full_path, &len, "%s%s", path, ent->d_name);
        entry = mk_dirhtml_create_element(ent->d_name,
                                          ent->d_type, full_path, list_len);

        mk_api->mem_free(full_path);
        full_path = NULL;

        if (!entry) {
            continue;
        }

        if (!list) {
            list = entry;
        }
        else {
            last->next = entry;
        }
        last = entry;
    }

    return list;
}

/* Read dirhtml config and themes */
int mk_dirhtml_conf(char *confdir)
{
    int ret = 0;
    unsigned long len;
    char *conf_file = NULL;

    mk_api->str_build(&conf_file, &len, "%s", confdir);

    /* Read configuration */
    ret = mk_dirhtml_read_config(conf_file);
    if (ret < 0) {
        return -1;
    }

    /*
     * This function will load the default theme setted in dirhtml_conf struct
     */
    return mk_dirhtml_theme_load();
}

/*
 * Read the main configuration file for dirhtml: dirhtml.conf,
 * it will alloc the dirhtml_conf struct
*/
int mk_dirhtml_read_config(char *path)
{
    unsigned long len;
    char *default_file = NULL;
    struct mk_config *conf;
    struct mk_config_section *section;
    struct file_info finfo;

    mk_api->str_build(&default_file, &len, "%sdirhtml.conf", path);
    conf = mk_api->config_create(default_file);
    if (!conf) {
        return -1;
    }

    section = mk_api->config_section_get(conf, "DIRLISTING");
    if (!section) {
        mk_err("Could not find DIRLISTING tag in configuration file");
        exit(EXIT_FAILURE);
    }

    /* alloc dirhtml config struct */
    dirhtml_conf = mk_api->mem_alloc(sizeof(struct dirhtml_config));
    dirhtml_conf->theme = mk_api->config_section_get_key(section, "Theme",
                                                         MK_CONFIG_VAL_STR);
    dirhtml_conf->theme_path = NULL;

    mk_api->str_build(&dirhtml_conf->theme_path, &len,
                      "%sthemes/%s/", path, dirhtml_conf->theme);

    mk_api->mem_free(default_file);

    if (mk_api->file_get_info(dirhtml_conf->theme_path,
                              &finfo, MK_FILE_READ) != 0) {
        mk_warn("Dirlisting: cannot load theme from '%s'", dirhtml_conf->theme_path);
        mk_warn("Dirlisting: unloading plugin");
        return -1;
    }

    return 0;
}

int mk_dirhtml_theme_load()
{
    /* Data */
    char *header, *entry, *footer;

    /* Load theme files */
    header = mk_dirhtml_load_file(MK_DIRHTML_FILE_HEADER);
    entry = mk_dirhtml_load_file(MK_DIRHTML_FILE_ENTRY);
    footer = mk_dirhtml_load_file(MK_DIRHTML_FILE_FOOTER);

    if (!header || !entry || !footer) {
        mk_api->mem_free(header);
        mk_api->mem_free(entry);
        mk_api->mem_free(footer);
        return -1;
    }

    /* Parse themes */
    mk_dirhtml_tpl_header = mk_dirhtml_template_create(header);
    mk_dirhtml_tpl_entry = mk_dirhtml_template_create(entry);
    mk_dirhtml_tpl_footer = mk_dirhtml_template_create(footer);

#ifdef DEBUG_THEME
    /* Debug data */
    mk_dirhtml_theme_debug(&mk_dirhtml_tpl_header);
    mk_dirhtml_theme_debug(&mk_dirhtml_tpl_entry);
    mk_dirhtml_theme_debug(&mk_dirhtml_tpl_footer);

#endif
    mk_api->mem_free(header);
    mk_api->mem_free(entry);
    mk_api->mem_free(footer);

    return 0;
}

#ifdef DEBUG_THEME
int mk_dirhtml_theme_debug(struct dirhtml_template **st_tpl)
{
    int i = 0;
    struct dirhtml_template *aux;

    aux = *st_tpl;

    printf("\n** DEBUG_THEME **");
    fflush(stdout);

    while (aux) {
        printf("\n%i) len=%i, tag_id=%i", i, aux->len, aux->tag_id);
        if (aux->tag_id >= 0) {
            printf(" (%s) ", aux->tags[aux->tag_id]);
        }
        fflush(stdout);
        aux = aux->next;
        i++;
    }
    return 0;
}
#endif

/* Search which tag exists first in content :
 * ex: %_html_title_%
 */
static int mk_dirhtml_theme_match_tag(char *content, char *tpl[])
{
    int i, len, match;

    for (i = 0; tpl[i]; i++) {
        len = strlen(tpl[i]);
        match = (int) mk_api->str_search_n(content, tpl[i], MK_STR_INSENSITIVE, len);
        if (match >= 0) {
            return i;
        }
    }

    return -1;
}

struct dirhtml_template *mk_dirhtml_template_create(char *content)
{
    int i = 0, cont_len;
    int pos, last = 0;          /* 0=search init, 1=search end */
    int n_tags = 0, tpl_idx = 0;

    char *_buf;
    int _len;

    /* Global keys */
    char **_tpl = 0;

    /* Template to return */
    struct dirhtml_template *st_tpl = 0;

    cont_len = strlen(content);
    if (cont_len <= 0) {
        return NULL;
    }

    /* Parsing content */
    while (i < cont_len) {
        pos = (int) mk_api->str_search(content + i,
                                       MK_DIRHTML_TAG_INIT, MK_STR_INSENSITIVE);

        if (pos < 0) {
            break;
        }

        /* Checking global tag, if it's not found, proceed with
         * 'entry tags'
         */
        _tpl = (char **) _tags_global;
        tpl_idx = mk_dirhtml_theme_match_tag(content + i + pos, _tpl);

        /* if global template do not match, use the entry tags */
        if (tpl_idx < 0) {
            _tpl = (char **) _tags_entry;
            tpl_idx = mk_dirhtml_theme_match_tag(content + i + pos, _tpl);
        }

        /* if tag found is known, we add them to our list */
        if (tpl_idx >= 0) {

            _buf = mk_api->str_copy_substr(content, i, i + pos);
            _len = strlen(_buf);

            /* Dummy if/else to create or pass a created st_tpl */
            if (!st_tpl) {
                st_tpl = mk_dirhtml_template_list_add(NULL,
                                                      _buf, _len, _tpl, -1);
            }
            else {
                mk_dirhtml_template_list_add(&st_tpl, _buf, _len, _tpl, -1);
            }
            i += (pos + strlen(_tpl[tpl_idx]));

            /* This means that a value need to be replaced */
            mk_dirhtml_template_list_add(&st_tpl, NULL, -1, _tpl, tpl_idx);
            n_tags++;
        }
        else {
            i++;
        }
    }

    if (last < cont_len) {
        _buf = mk_api->str_copy_substr(content, i, cont_len);
        _len = strlen(_buf);

        if (n_tags <= 0) {
            st_tpl = mk_dirhtml_template_list_add(NULL, _buf, _len, _tpl, -1);
        }
        else {
            mk_dirhtml_template_list_add(&st_tpl, _buf, _len, _tpl, -1);
        }
    }

    return st_tpl;
}

struct dirhtml_template *mk_dirhtml_template_list_add(struct dirhtml_template **header,
                                                      char *buf, int len, char **tpl, int tag_id)
{
    struct dirhtml_template *node, *aux;

    node = mk_api->mem_alloc_z(sizeof(struct dirhtml_template));
    if (!node) {
        return NULL;
    }

    node->buf = buf;
    node->len = len;
    node->tag_id = tag_id;
    node->tags = tpl;
    node->next = NULL;

    if (!header || !(*header)) {
        return (struct dirhtml_template *) node;
    }

    aux = *header;
    while ((*aux).next != NULL) {
        aux = (*aux).next;
    }

    (*aux).next = node;
    return (struct dirhtml_template *) node;
}

static int mk_dirhtml_template_len(struct dirhtml_template *tpl)
{
    int len = 0;
    struct dirhtml_template *aux;

    aux = tpl;
    while (aux) {
        len++;
        aux = aux->next;
    }

    return len;
}

static struct mk_iov *mk_dirhtml_theme_compose(struct dirhtml_template *template,
                                               struct dirhtml_value *values)
{
    /*
     * template = struct { char buf ; int len, int tag }
     * values = struct {int tag, char *value, struct *next}
     */

    struct mk_iov *iov;
    struct dirhtml_template *tpl = template;
    struct dirhtml_value *val = values;

    int tpl_len;

    tpl_len = mk_dirhtml_template_len(template);

    /* we duplicate the lenght in case we get separators */
    iov = (struct mk_iov *) mk_api->iov_create(1 + tpl_len * 2, 1);
    tpl = template;

    while (tpl) {
        /* check for dynamic value */
        if (!tpl->buf && tpl->tag_id >= 0) {
            val = values;
            while (val) {
                if (val->tags == tpl->tags && val->tag_id == tpl->tag_id) {
                    mk_api->iov_add(iov,
                                    val->value, val->len,
                                    MK_FALSE);
                    mk_api->iov_add(iov,
                                    val->sep.data, val->sep.len,
                                    MK_FALSE);
                    break;
                }
                else {
                    val = val->next;
                }
            }
        }
        /* static */
        else {
            mk_api->iov_add(iov,
                            tpl->buf, tpl->len,
                            MK_FALSE);
        }
        tpl = tpl->next;
    }

    return iov;
}

struct dirhtml_value *mk_dirhtml_tag_assign(struct dirhtml_value **values,
                                            int tag_id, mk_ptr_t sep,
                                            char *value, char **tags)
{
    struct dirhtml_value *check, *aux = 0;

    aux = mk_api->mem_alloc(sizeof(struct dirhtml_value));
    if (!aux) {
        return NULL;
    }

    aux->tag_id = tag_id;
    aux->value = value;
    aux->sep = sep;
    aux->tags = tags;

    if (value) {
        aux->len = strlen(value);
    }
    else {
        aux->len = -1;
    }

    aux->next = NULL;

    if (!values) {
        return (struct dirhtml_value *) aux;
    }

    check = *values;
    while ((*check).next) {
        check = (*check).next;
    }

    (*check).next = aux;


    return (struct dirhtml_value *) aux;
}

static void mk_dirhtml_tag_free_list(struct dirhtml_value **list)
{
    struct dirhtml_value *prev=0, *target;

    target = *list;
    while (target) {
        while ((*target).next) {
            prev = target;
            target = (*target).next;
        }

        mk_api->mem_free(target);

        if (target == *list) {
            break;
        }
        (*prev).next = NULL;
        target = *list;
    }
    *list = NULL;
}

char *mk_dirhtml_load_file(char *filename)
{
    char *tmp = 0, *data = 0;
    unsigned long len;

    mk_api->str_build(&tmp, &len, "%s%s", dirhtml_conf->theme_path, filename);

    if (!tmp) {
        return NULL;
    }

    data = mk_api->file_to_buffer(tmp);
    mk_api->mem_free(tmp);

    if (!data) {
        return NULL;
    }

    return (char *) data;
}

static int mk_dirhtml_entry_cmp(const void *a, const void *b)
{
    struct mk_f_list *const *f_a = a;
    struct mk_f_list *const *f_b = b;

    return strcasecmp((*f_a)->name, (*f_b)->name);
}

static void mk_dirhtml_free_list(struct mk_f_list **toc, unsigned long len)
{
    unsigned int i;
    struct mk_f_list *entry;

    for (i = 0; i < len; i++) {
        entry = toc[i];

        if (entry->type != DT_DIR) {
            mk_api->mem_free(entry->size);
        }
        mk_api->mem_free(entry);
    }

    mk_api->mem_free(toc);
}

static inline struct mk_iov *enqueue_row(int i, struct mk_dirhtml_request *request)
{
    mk_ptr_t sep;
    struct mk_iov *iov_entry;
    struct dirhtml_value *values_entry = NULL;

    /* %_target_title_% */
    if (request->toc[i]->type == DT_DIR) {
        sep = mk_dir_iov_slash;
    }
    else {
        sep = mk_dir_iov_none;
    }

    /* target title */
    values_entry = mk_dirhtml_tag_assign(NULL, 0, sep,
                                         request->toc[i]->name,
                                         (char **) _tags_entry);

    /* target url */
    mk_dirhtml_tag_assign(&values_entry, 1, sep,
                          request->toc[i]->name, (char **) _tags_entry);

    /* target name */
    mk_dirhtml_tag_assign(&values_entry, 2, sep,
                          request->toc[i]->name, (char **) _tags_entry);

    /* target modification time */
    mk_dirhtml_tag_assign(&values_entry, 3, mk_dir_iov_none,
                          request->toc[i]->ft_modif, (char **) _tags_entry);

    /* target size */
    mk_dirhtml_tag_assign(&values_entry, 4, mk_dir_iov_none,
                          request->toc[i]->size, (char **) _tags_entry);

    iov_entry = mk_dirhtml_theme_compose(mk_dirhtml_tpl_entry,
                                         values_entry);

    /* free entry list */
    mk_dirhtml_tag_free_list(&values_entry);
    return iov_entry;
}

/* Release all resources for a given Request context */
void mk_dirhtml_cleanup(struct mk_dirhtml_request *req)
{
    PLUGIN_TRACE("release resources");

    mk_api->iov_free(req->iov_header);
    if (req->iov_entry) {
        mk_api->iov_free(req->iov_entry);
    }
    mk_api->iov_free(req->iov_footer);
    mk_dirhtml_free_list(req->toc, req->toc_len);
    mk_api->mem_free(req);
}

void mk_dirhtml_cb_complete(struct mk_stream *stream)
{
    struct mk_dirhtml_request *req = stream->data;
    mk_dirhtml_cleanup(req);
}

void mk_dirhtml_cb_error(struct mk_stream *stream, int status)
{
#ifndef TRACE
    (void) status;
#endif
    struct mk_dirhtml_request *req = stream->data;

    PLUGIN_TRACE("exception: %i", status);
    mk_dirhtml_cleanup(req);
}

void mk_dirhtml_cb_body_rows(struct mk_stream *stream)
{
    struct mk_dirhtml_request *req = stream->data;
    struct mk_channel *channel = stream->channel;

    if (req->toc_idx >= req->toc_len) {
        req->body.preserve = MK_FALSE;

        /* No more rows to add, just link the page footer */
        mk_api->stream_set(&req->footer,           /* stream            */
                           MK_STREAM_IOV,          /* type              */
                           channel,                /* channel           */
                           req->iov_footer,        /* buffer            */
                           -1,                     /* buffer size       */
                           req,                    /* custom data       */
                           mk_dirhtml_cb_complete, /* on_finish         */
                           NULL,                   /* on_bytes_consumed */
                           mk_dirhtml_cb_error);   /* on_error          */
        return;
    }

    if (req->toc_idx > 0) {
        mk_api->iov_free(req->iov_entry);
        mk_list_del(&stream->_head);
    }

    req->iov_entry = enqueue_row(req->toc_idx, req);
    mk_api->stream_set(&req->body,
                       MK_STREAM_IOV,
                       channel,
                       req->iov_entry,
                       -1,
                       (void *) req,
                       mk_dirhtml_cb_body_rows,
                       NULL,
                       mk_dirhtml_cb_error);
    req->body.preserve = MK_TRUE;
    req->toc_idx++;
}

/*
 * The HTTP Headers were sent, now start registering the
 * rows for each directory entry.
 */
void cb_header_finish(struct mk_stream *stream)
{
    mk_dirhtml_cb_body_rows(stream);
}

int mk_dirhtml_init(struct mk_http_session *cs, struct mk_http_request *sr)
{
    DIR *dir;
    unsigned int i = 0;
    char *title = 0;
    struct mk_f_list *file_list, *entry;
    struct dirhtml_value *values_global = 0;
    struct mk_dirhtml_request *request;

    if (!(dir = opendir(sr->real_path.data))) {
        return -1;
    }

    /* Create the main context */
    request = mk_api->mem_alloc(sizeof(struct mk_dirhtml_request));
    request->state   = MK_DIRHTML_STATE_HTTP_HEADER;
    request->dir     = dir;
    request->toc_idx = 0;
    request->cs      = cs;
    request->sr      = sr;

    file_list = mk_dirhtml_create_list(dir, sr->real_path.data,
                                       &request->toc_len);

    /* Building headers */
    mk_api->header_set_http_status(sr, MK_HTTP_OK);
    sr->headers.cgi = SH_CGI;
    sr->headers.breakline = MK_HEADER_BREAKLINE;
    sr->headers.content_type = mk_dirhtml_default_mime;
    sr->headers.content_length = -1;

    if (sr->protocol >= MK_HTTP_PROTOCOL_11) {
        sr->headers.transfer_encoding = MK_HEADER_TE_TYPE_CHUNKED;
    }

    /*
     * Creating response template
     */

    /* Set %_html_title_% */
    title = mk_api->pointer_to_buf(sr->uri_processed);
    values_global = mk_dirhtml_tag_assign(NULL, 0, mk_dir_iov_none,
                                          title,
                                          (char **) _tags_global);

    /* Set %_theme_path_% */
    mk_dirhtml_tag_assign(&values_global, 1, mk_dir_iov_none,
                          dirhtml_conf->theme_path, (char **) _tags_global);

    /* HTML Header */
    request->iov_header = mk_dirhtml_theme_compose(mk_dirhtml_tpl_header,
                                                   values_global);

    /* HTML Footer */
    request->iov_footer = mk_dirhtml_theme_compose(mk_dirhtml_tpl_footer,
                                                   values_global);

    /* Creating table of contents and sorting */
    request->toc = mk_api->mem_alloc(sizeof(struct mk_f_list *) * request->toc_len);
    entry = file_list;

    i = 0;
    while (entry) {
        request->toc[i] = entry;
        i++;
        entry = entry->next;
    }
    qsort(request->toc,
          request->toc_len,
          sizeof(*request->toc),
          mk_dirhtml_entry_cmp);

    /* Prepare HTTP response headers */
    mk_api->header_prepare(cs, sr);

    mk_api->stream_set(&request->header,     /* stream            */
                       MK_STREAM_IOV,        /* type              */
                       &cs->channel,         /* channel           */
                       request->iov_header,  /* buffer            */
                       -1,                   /* buffer size       */
                       request,              /* custom data       */
                       cb_header_finish,     /* on_finish         */
                       NULL,                 /* on_bytes_consumed */
                       mk_dirhtml_cb_error); /* on_error          */

    /*
     * Dispatch, once we get a callback on mk_dirhtml_cb_finish, we
     * continue appending data to the channel
     */
    mk_api->channel_write(&cs->channel);

    return 0;
}

int mk_dirlisting_plugin_init(struct plugin_api **api, char *confdir)
{
    mk_api = *api;

    return mk_dirhtml_conf(confdir);
}

int mk_dirlisting_plugin_exit()
{
    return 0;
}

int mk_dirlisting_stage30(struct mk_plugin *plugin, struct mk_http_session *cs,
                          struct mk_http_request *sr)
{
    (void) plugin;

    /* validate file_info */
    if (sr->file_info.size < 0) {
        return MK_PLUGIN_RET_NOT_ME;
    }

    /* This plugin just handle directories */
    if (sr->file_info.is_directory == MK_FALSE) {
        return MK_PLUGIN_RET_NOT_ME;
    }

    PLUGIN_TRACE("Dirlisting attending socket %i", cs->socket);
    if (mk_dirhtml_init(cs, sr)) {
        /*
         * If we failed here, we cannot return RET_END - that causes a mk_bug.
         * dirhtml_init only fails if opendir fails. Usually we're at full
         * capacity then and can't open new files.
         */
        return MK_PLUGIN_RET_CLOSE_CONX;
    }

    return MK_PLUGIN_RET_END;
}

struct mk_plugin_stage mk_plugin_stage_dirlisting = {
    .stage30      = &mk_dirlisting_stage30
};

struct mk_plugin mk_plugin_dirlisting = {
    /* Identification */
    .shortname     = "dirlisting",
    .name          = "Directory Listing",
    .version       = MK_VERSION_STR,
    .hooks         = MK_PLUGIN_STAGE,

    /* Init / Exit */
    .init_plugin   = mk_dirlisting_plugin_init,
    .exit_plugin   = mk_dirlisting_plugin_exit,

    /* Init Levels */
    .master_init   = NULL,
    .worker_init   = NULL,

    /* Type */
    .stage         = &mk_plugin_stage_dirlisting
};
