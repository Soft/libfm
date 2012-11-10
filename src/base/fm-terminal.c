/*
 *      fm-terminal.c
 *
 *      Copyright 2012 Andriy Grytsenko (LStranger) <andrej@rep.kiev.ua>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

/**
 * SECTION:fm-terminal
 * @short_description: Terminals representation for libfm.
 * @title: FmTerminal
 *
 * @include: libfm/fm.h
 *
 * The FmTerminal object represents description how applications which
 * require start in terminal should be started.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gio/gdesktopappinfo.h>
#include <string.h>

#include "fm-terminal.h"
#include "fm-config.h"

struct _FmTerminalClass
{
    GObjectClass parent;
};

static void fm_terminal_finalize(GObject *object);

G_DEFINE_TYPE(FmTerminal, fm_terminal, G_TYPE_OBJECT);

static void fm_terminal_class_init(FmTerminalClass *klass)
{
    GObjectClass *g_object_class;

    g_object_class = G_OBJECT_CLASS(klass);
    g_object_class->finalize = fm_terminal_finalize;
}

static void fm_terminal_finalize(GObject *object)
{
    FmTerminal* self;
    g_return_if_fail(object != NULL);
    g_return_if_fail(FM_IS_TERMINAL(object));

    self = (FmTerminal*)object;
    g_free(self->program);
    g_free(self->open_arg);
    g_free(self->noclose_arg);
    g_free(self->launch);
    g_free(self->desktop_id);
    g_free(self->custom_args);

    G_OBJECT_CLASS(fm_terminal_parent_class)->finalize(object);
}

static void fm_terminal_init(FmTerminal *self)
{
}

static FmTerminal* fm_terminal_new(void)
{
    return (FmTerminal*)g_object_new(FM_TERMINAL_TYPE, NULL);
}


static GSList *terminals = NULL;
static FmTerminal *default_terminal = NULL;

static void on_terminal_changed(FmConfig *cfg, gpointer unused)
{
    FmTerminal *term;
    gsize n;
    GSList *l;
    gchar *name, *basename;

    if(default_terminal)
        g_object_unref(default_terminal);
    default_terminal = NULL;
    if(cfg->terminal == NULL)
        return;

    for(n = 0; cfg->terminal[n] && cfg->terminal[n] != ' '; n++);
    name = g_strndup(cfg->terminal, n);
    basename = strrchr(name, '/');
    if(basename)
        basename++;
    else
        basename = name;
    /* g_debug("terminal in FmConfig: %s, args=%s", name, &cfg->terminal[n]); */
    for(l = terminals; l; l = l->next)
        if(strcmp(basename, ((FmTerminal*)l->data)->program) == 0)
            break;
    if(l)
    {
        if(name[0] != '/') /* not full path; call by basename */
        {
            g_free(name);
            term = g_object_ref(l->data);
        }
        else /* call by full path: add new description and fill from database */
        {
            term = fm_terminal_new();
            term->program = name;
            term->open_arg = g_strdup(((FmTerminal*)l->data)->open_arg);
            term->noclose_arg = g_strdup(((FmTerminal*)l->data)->noclose_arg);
            term->launch = g_strdup(((FmTerminal*)l->data)->launch);
            term->desktop_id = g_strdup(((FmTerminal*)l->data)->desktop_id);
        }
    }
    else /* unknown terminal */
    {
        term = fm_terminal_new();
        term->program = name;
        term->open_arg = g_strdup("-e"); /* assume it is default */
    }
    default_terminal = term;
    g_free(term->custom_args);
    term->custom_args = NULL;
    if(cfg->terminal[n] == ' ' && cfg->terminal[n+1])
    {
        term->custom_args = g_strdup(&cfg->terminal[n+1]);
        /* support for old style terminal line alike 'xterm -e %s' */
        name = strchr(term->custom_args, '%');
        if(name)
        {
            /* skip end spaces */
            while(name > term->custom_args && name[-1] == ' ')
                name--;
            /* drop '-e' or '-x' */
            if(name > term->custom_args + 1 && name[-2] == '-')
            {
                name -= 2;
                /* skip end spaces */
                while(name > term->custom_args && name[-1] == ' ')
                    name--;
            }
            if(name > term->custom_args)
                *name = '\0'; /* cut the line */
            else
            {
                g_free(term->custom_args);
                term->custom_args = NULL;
            }
        }
    }
}

/* init terminal list from config */
void _fm_terminal_init(void)
{
    GKeyFile *kf;
    gsize i, n;
    gchar **programs;
    FmTerminal *term;

    /* read system terminals file */
    kf = g_key_file_new();
    if(g_key_file_load_from_file(kf, PACKAGE_DATA_DIR "/terminals.list", 0, NULL))
    {
        programs = g_key_file_get_groups(kf, &n);
        if(programs)
        {
            for(i = 0; i < n; ++i)
            {
                /* g_debug("found terminal configuration: %s", programs[i]); */
                term = fm_terminal_new();
                term->program = programs[i];
                term->open_arg = g_key_file_get_string(kf, programs[i],
                                                       "open_arg", NULL);
                term->noclose_arg = g_key_file_get_string(kf, programs[i],
                                                          "noclose_arg", NULL);
                term->launch = g_key_file_get_string(kf, programs[i],
                                                     "launch", NULL);
                term->desktop_id = g_key_file_get_string(kf, programs[i],
                                                         "desktop_id", NULL);
                terminals = g_slist_append(terminals, term);
            }
            g_free(programs); /* strings in the vector are stolen by objects */
        }
    }
    g_key_file_free(kf);
    /* TODO: read user terminals file? */
    /* read from config */
    on_terminal_changed(fm_config, NULL);
    /* monitor the config */
    g_signal_connect(fm_config, "changed::terminal",
                     G_CALLBACK(on_terminal_changed), NULL);
}

/* free all resources */
void _fm_terminal_finalize(void)
{
    /* cancel monitor of config */
    g_signal_handlers_disconnect_by_func(fm_config, on_terminal_changed, NULL);
    /* free the data */
    g_slist_foreach(terminals, (GFunc)g_object_unref, NULL);
    g_slist_free(terminals);
    terminals = NULL;
    if(default_terminal)
        g_object_unref(default_terminal);
    default_terminal = NULL;
}

/**
 * fm_terminal_dup_default
 * @error: (allow-none): location of error to set
 *
 * Retrieves description of terminal which is defined in libfm config.
 * Returned data should be freed with g_object_unref() after usage.
 *
 * This API is not thread-safe.
 *
 * Returns: (transfer full): terminal descriptor or %NULL if no terminal is set.
 *
 * Since: 1.2.0
 */
FmTerminal* fm_terminal_dup_default(GError **error)
{
    if(default_terminal)
        return g_object_ref(default_terminal);
    g_set_error_literal(error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING,
                        _("No terminal emulator is set in libfm config"));
    return NULL;
}

/**
 * fm_terminal_launch
 * @dir: (allow-none): a directory to launch
 * @error: (allow-none): location of error to set
 *
 * Spawns a terminal window in requested @dir. If @dir is %NULL then it
 * will be spawned in current working directory.
 *
 * Returns: %TRUE if spawn was succesful.
 *
 * Since: 1.2.0
 */
gboolean fm_terminal_launch(const gchar *dir, GError **error)
{
    GDesktopAppInfo *appinfo = NULL;
    const gchar *cmd;
    gchar *_cmd = NULL;
    gchar **argv;
    gint argc;
    gboolean ret;

    if(!default_terminal)
    {
        g_set_error_literal(error, G_SHELL_ERROR, G_SHELL_ERROR_EMPTY_STRING,
                            _("No terminal emulator is set in libfm config"));
        return FALSE;
    }
    if(default_terminal->desktop_id)
        appinfo = g_desktop_app_info_new(default_terminal->desktop_id);
    if(appinfo)
        /* FIXME: is it possible to have some %U there? */
        cmd = g_app_info_get_commandline(G_APP_INFO(appinfo));
    else if(default_terminal->launch)
        cmd = _cmd = g_strdup_printf("%s %s", default_terminal->program,
                                     default_terminal->launch);
    else
        cmd = default_terminal->program;
    if(!g_shell_parse_argv(cmd, &argc, &argv, error))
        argv = NULL;
    g_free(_cmd);
    if(appinfo)
        g_object_unref(appinfo);
    if(!argv) /* parsing failed */
        return FALSE;
    ret = g_spawn_async(dir, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, error);
    g_strfreev(argv);
    return ret;
}