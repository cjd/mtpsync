#ifndef MAINHEADER_INCLUDED
#define MAINHEADER_INCLUDED
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>
#include <libmtp.h>
#include <glib.h>
#include <id3tag.h>
#include <mad.h>
#include <sys/mman.h>

#define USE_LOCAL 1
#define USE_REMOTE 2

#define FILE_TYPE 0
#define FOLDER_TYPE 1
const int ASK;
const int LOCAL_TO_REMOTE;
const int REMOTE_TO_LOCAL;
const int IGNORE;
GConfClient *gconf;
LIBMTP_mtpdevice_t *device;
LIBMTP_folder_t *folders;
LIBMTP_file_t *files;
LIBMTP_playlist_t *playlists;
GSList *updates;
gchar *musicpath;
gchar *playpath;
gchar *picturepath;
gchar *videopath;
gchar *organizerpath;
gchar *zencastpath;
gboolean nogui;
gboolean force_direction;
gboolean folders_changed;
guint context_id;
GtkWidget *windowMain;
GtkWidget *dialogSetup;
GtkWidget *dialogProgress;
GtkProgressBar *progressbar;
GtkListStore *store;


struct object
{
    int type;			// File or folder
    char *path;			// Path of item
    int local_size;		// Size of item
    int remote_size;		// Size of item
    int sync_direction;		// Direction of sync
    int remote;			// Exists on device
    int local;			// Exists on disk
    int item_id;		// Item id on device
    int top_parent;		// Id of top parent
};

enum
{
    ACTION_COLUMN,
    MESSAGE_COLUMN,
    ITEMID_COLUMN,
    PATH_COLUMN,
    N_COLUMNS
};

static GOptionEntry entries[] = {
    {"console", 'c', 0, G_OPTION_ARG_NONE, &nogui, "Run in console", NULL},
    {"music", 'm', 0, G_OPTION_ARG_STRING, &musicpath, "Path to Music", NULL},
    {"playlists", 'p', 0, G_OPTION_ARG_STRING, &playpath, "Path to Playlists",
     NULL},
    {"images", 'i', 0, G_OPTION_ARG_STRING, &picturepath,
     "Path to Photos/images", NULL},
    {"videos", 'v', 0, G_OPTION_ARG_STRING, &videopath, "Path to Videos",
     NULL},
    {"organizer", 'o', 0, G_OPTION_ARG_STRING, &organizerpath,
     "Path to Organizer", NULL},
    {"zencast", 'z', 0, G_OPTION_ARG_STRING, &zencastpath, "Path to Zencasts",
     NULL},
    {NULL}
};

static gint compare_path (gconstpointer, gconstpointer);
void update_table (struct object *);
void send_items (struct object *);
void delete_items (struct object *);

#endif
