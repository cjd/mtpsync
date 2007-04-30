#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/stat.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "main.h"
#include "interface.h"
#include "support.h"
#include "id3read.h"

/* Set initial values for global variables */
const int ASK = 0;
const int LOCAL_TO_REMOTE = 1;
const int REMOTE_TO_LOCAL = 2;
const int IGNORE = 3;
LIBMTP_mtpdevice_t *device = NULL;
LIBMTP_folder_t *folders = NULL;
LIBMTP_file_t *files = NULL;
LIBMTP_playlist_t *playlists = NULL;
GSList *updates = NULL;
gchar *musicpath = NULL;
gchar *playpath = NULL;
gchar *picturepath = NULL;
gchar *videopath = NULL;
gchar *organizerpath = NULL;
gchar *zencastpath = NULL;
gboolean nogui = FALSE;
gboolean force_direction = 0;
gboolean folders_changed = FALSE;
guint context_id = 0;
GtkProgressBar * progressbar = NULL;


/* Find the file type based on extension */
static LIBMTP_filetype_t
find_filetype (const gchar * filename)
{
    gchar **fields;
    fields = g_strsplit (filename, ".", -1);
    gchar *ptype;
    ptype = g_strdup (fields[g_strv_length (fields) - 1]);
    g_strfreev (fields);
    LIBMTP_filetype_t filetype;
    /* This need to be kept constantly updated as new file types arrive. */
    if (!strcasecmp (ptype, "wav")) {
        filetype = LIBMTP_FILETYPE_WAV;
    } else if (!strcasecmp (ptype, "mp3")) {
        filetype = LIBMTP_FILETYPE_MP3;
    } else if (!strcasecmp (ptype, "wma")) {
        filetype = LIBMTP_FILETYPE_WMA;
    } else if (!strcasecmp (ptype, "ogg")) {
        filetype = LIBMTP_FILETYPE_OGG;
    } else if (!strcasecmp (ptype, "mp4")) {
        filetype = LIBMTP_FILETYPE_MP4;
    } else if (!strcasecmp (ptype, "wmv")) {
        filetype = LIBMTP_FILETYPE_WMV;
    } else if (!strcasecmp (ptype, "avi")) {
        filetype = LIBMTP_FILETYPE_AVI;
    } else if (!strcasecmp (ptype, "mpeg") || !strcasecmp (ptype, "mpg")) {
        filetype = LIBMTP_FILETYPE_MPEG;
    } else if (!strcasecmp (ptype, "asf")) {
        filetype = LIBMTP_FILETYPE_ASF;
    } else if (!strcasecmp (ptype, "qt") || !strcasecmp (ptype, "mov")) {
        filetype = LIBMTP_FILETYPE_QT;
    } else if (!strcasecmp (ptype, "wma")) {
        filetype = LIBMTP_FILETYPE_WMA;
    } else if (!strcasecmp (ptype, "jpg") || !strcasecmp (ptype, "jpeg")) {
        filetype = LIBMTP_FILETYPE_JPEG;
    } else if (!strcasecmp (ptype, "jfif")) {
        filetype = LIBMTP_FILETYPE_JFIF;
    } else if (!strcasecmp (ptype, "tif") || !strcasecmp (ptype, "tiff")) {
        filetype = LIBMTP_FILETYPE_TIFF;
    } else if (!strcasecmp (ptype, "bmp")) {
        filetype = LIBMTP_FILETYPE_BMP;
    } else if (!strcasecmp (ptype, "gif")) {
        filetype = LIBMTP_FILETYPE_GIF;
    } else if (!strcasecmp (ptype, "pic") || !strcasecmp (ptype, "pict")) {
        filetype = LIBMTP_FILETYPE_PICT;
    } else if (!strcasecmp (ptype, "png")) {
        filetype = LIBMTP_FILETYPE_PNG;
    } else if (!strcasecmp (ptype, "wmf")) {
        filetype = LIBMTP_FILETYPE_WINDOWSIMAGEFORMAT;
    } else if (!strcasecmp (ptype, "ics")) {
        filetype = LIBMTP_FILETYPE_VCALENDAR2;
    } else if (!strcasecmp (ptype, "exe") || !strcasecmp (ptype, "com") ||
               !strcasecmp (ptype, "bat") || !strcasecmp (ptype, "dll") ||
               !strcasecmp (ptype, "sys")) {
        filetype = LIBMTP_FILETYPE_WINEXEC;
    } else {
        /* Tagging as unknown file type */
        filetype = LIBMTP_FILETYPE_UNKNOWN;
    }
    return filetype;
}

/* XING parsing is from the MAD winamp input plugin */

struct xing
    {
        int flags;
        unsigned long frames;
        unsigned long bytes;
        unsigned char toc[100];
        long scale;
    };

enum
    {
        XING_FRAMES = 0x0001,
        XING_BYTES = 0x0002,
        XING_TOC = 0x0004,
        XING_SCALE = 0x0008
    };

# define XING_MAGIC     (('X' << 24) | ('i' << 16) | ('n' << 8) | 'g')


/* Following two function are adapted from mad_timer, from the
 *    libmad distribution */
static int
parse_xing (struct xing *xing, struct mad_bitptr ptr, unsigned int bitlen)
{
    if (bitlen < 64 || mad_bit_read (&ptr, 32) != XING_MAGIC)
        goto fail;

    xing->flags = mad_bit_read (&ptr, 32);
    bitlen -= 64;

    if (xing->flags & XING_FRAMES) {
        if (bitlen < 32)
            goto fail;

        xing->frames = mad_bit_read (&ptr, 32);
        bitlen -= 32;
    }

    if (xing->flags & XING_BYTES) {
        if (bitlen < 32)
            goto fail;

        xing->bytes = mad_bit_read (&ptr, 32);
        bitlen -= 32;
    }

    if (xing->flags & XING_TOC) {
        int i;

        if (bitlen < 800)
            goto fail;

        for (i = 0; i < 100; ++i)
            xing->toc[i] = mad_bit_read (&ptr, 8);

        bitlen -= 800;
    }

    if (xing->flags & XING_SCALE) {
        if (bitlen < 32)
            goto fail;

        xing->scale = mad_bit_read (&ptr, 32);
        bitlen -= 32;
    }

    return 1;
  fail:
    xing->flags = 0;
    return 0;
}

int
scan (void const *ptr, ssize_t len)
{
    struct mad_stream stream;
    struct mad_header header;
    struct xing xing;
    //g_debug("scan: %d",len);


    unsigned long bitrate = 0;
    int has_xing = 0;
    int is_vbr = 0;
    int duration = 0;
    mad_stream_init (&stream);
    mad_header_init (&header);

    mad_stream_buffer (&stream, ptr, len);

    int num_frames = 0;

    /* There are three ways of calculating the length of an mp3:
       1) Constant bitrate: One frame can provide the information
       needed: # of frames and duration. Just see how long it
       is and do the division.
       2) Variable bitrate: Xing tag. It provides the number of
       frames. Each frame has the same number of samples, so
       just use that.
       3) All: Count up the frames and duration of each frames
       by decoding each one. We do this if we've no other
       choice, i.e. if it's a VBR file with no Xing tag.
     */

    while (1) {
        if (mad_header_decode (&header, &stream) == -1) {
            if (MAD_RECOVERABLE (stream.error))
                continue;
            else
                break;
        }

        /* Limit xing testing to the first frame header */
        if (!num_frames++) {
            if (parse_xing (&xing, stream.anc_ptr, stream.anc_bitlen)) {
                is_vbr = 1;

                if (xing.flags & XING_FRAMES) {
                    /* We use the Xing tag only for frames. If it doesn't have
                       that information, it's useless to us and we have to
                       treat it as a normal VBR file */
                    has_xing = 1;
                    num_frames = xing.frames;
                    break;
                }
            }
        }

        /* Test the first n frames to see if this is a VBR file */
        if (!is_vbr && !(num_frames > 20)) {
            if (bitrate && header.bitrate != bitrate) {
                is_vbr = 1;
            }

            else {
                bitrate = header.bitrate;
            }
        }

        /* We have to assume it's not a VBR file if it hasn't already been
           marked as one and we've checked n frames for different bitrates */
        else if (!is_vbr) {
            break;
        }

        duration = header.duration.seconds;
    }

    if (!is_vbr) {
        double time = (len * 8.0) / (header.bitrate);   /* time in seconds */
        double timefrac = (double) time - ((long) (time));
        long nsamples = 32 * MAD_NSBSAMPLES (&header);  /* samples per frame */

        /* samplerate is a constant */
        num_frames = (long) (time * header.samplerate / nsamples);

        duration = (int) time;
        //g_debug("d:%d",duration);
    }

    else if (has_xing) {
        /* modify header.duration since we don't need it anymore */
        mad_timer_multiply (&header.duration, num_frames);
        duration = header.duration.seconds;
    }

    else {
        /* the durations have been added up, and the number of frames
           counted. We do nothing here. */
    }

    mad_header_finish (&header);
    mad_stream_finish (&stream);
    return duration;
}

/* Find the folder_id of a given path
 * Runs by walking through folders structure */
static int
lookup_folder_id (LIBMTP_folder_t * folder, gchar * path, gchar * parent)
{
    //g_debug("lookup_folder_id %s:%s",path,parent);
    int ret = -1;
    if (folder == NULL) {
        return ret;
    }
    gchar *current;
    current = g_strconcat (parent, "/", folder->name, NULL);
    if (strcasecmp (path, current) == 0) {
        g_free (current);
        return folder->folder_id;
    }
    if (strncasecmp (path, current, strlen (current)) == 0) {
        ret = lookup_folder_id (folder->child, path, current);
    }
    g_free (current);
    if (ret >= 0) {
        return ret;
    }
    ret = lookup_folder_id (folder->sibling, path, parent);
    return ret;
}

/* Find the length of a song
 * Takes a file number as option
 * Returns length in ms */
int
calc_length (int f)
{
    struct stat filestat;
    void *fdm;
    char buffer[3];

    fstat (f, &filestat);

    /* TAG checking is adapted from XMMS */
    int length = filestat.st_size;

    if (lseek (f, -128, SEEK_END) < 0) {
        /* File must be very short or empty. Forget it. */
        return -1;
    }

    if (read (f, buffer, 3) != 3) {
        return -1;
    }

    if (!strncmp (buffer, "TAG", 3)) {
        length -= 128;          /* Correct for id3 tags */
    }

    fdm = mmap (0, length, PROT_READ, MAP_SHARED, f, 0);
    if (fdm == MAP_FAILED) {
        g_error ("Map failed");
        return -1;
    }

    /* Scan the file for a XING header, or calculate the length,
       or just scan the whole file and add everything up. */
    int duration = scan (fdm, length);

    if (munmap (fdm, length) == -1) {
        g_error ("Unmap failed");
        return -1;
    }

    lseek (f, 0, SEEK_SET);
    return duration;
}


/* Scan device for playlists
 * Finds filesize by determining how large the m3u file would be */
void
scan_playlists ()
{
    LIBMTP_playlist_t *playlist;
    LIBMTP_folder_t *base_folder;
    playlist = playlists;
    base_folder = LIBMTP_Find_Folder (folders, device->default_music_folder);
    int prefix_size = strlen (base_folder->name);
    base_folder =
      LIBMTP_Find_Folder (folders, device->default_playlist_folder);
    struct object *item;
    struct object *tmpfile = g_malloc0 (sizeof (struct object));
    while (playlist != NULL) {
        struct object *tmppath;
        tmpfile->path =
          g_strconcat (base_folder->name, "/", playlist->name, ".m3u", NULL);
        tmpfile->item_id = playlist->playlist_id;
        tmpfile->type = FILE_TYPE;
        tmpfile->remote = TRUE;
        tmpfile->local = FALSE;
        tmpfile->sync_direction = ASK;
        tmpfile->remote_size = 0;
        int i;
        for (i = 0; i < playlist->no_tracks; i++) {
            LIBMTP_file_t *file;
            LIBMTP_folder_t *folder;
            file = LIBMTP_Get_Filemetadata (device, playlist->tracks[i]);
            if (file != NULL) {
                int parent_id = file->parent_id;
                tmpfile->remote_size =
                  tmpfile->remote_size + strlen (file->filename) -
                  prefix_size;
                while (parent_id != 0) {
                    folder = LIBMTP_Find_Folder (folders, parent_id);
                    parent_id = folder->parent_id;
                    tmpfile->remote_size =
                      tmpfile->remote_size + strlen (folder->name) + 1;
                }
            }
        }
        GSList *le;
        le = g_slist_find_custom (updates, tmpfile, compare_path);
        if (le != NULL) {
            item = le->data;
            item->remote = TRUE;
            item->remote_size = tmpfile->remote_size;
            item->item_id = tmpfile->item_id;
        } else {
            updates = g_slist_prepend (updates, tmpfile);
        }
        g_free (tmppath);
        playlist = playlist->next;
    }
}

/* Scan filesystem starting at basepath
 * Takes:
 *  current path (since it runs recursively)
 *  prefix (base path on remote device)
 *  basepath (were to start searching on local filesystem) */
void
scan_tree (const gchar * path, const gchar * prefix, const gchar * basepath)
{
    if (basepath == NULL)
        return;
    GDir *dir;
    gchar *tmppath;
    gchar *tmppath_gdir;
    gchar *tmpdir;
    tmpdir = g_strconcat (basepath, "/", path, NULL);
    dir = g_dir_open (tmpdir, 0, NULL);
    g_free (tmpdir);
    if (dir == NULL)
        return;
    struct object *folder = g_malloc0 (sizeof (struct object));
    folder->type = FOLDER_TYPE;
    folder->path = g_strconcat (prefix, path, NULL);
    folder->local_size = 0;
    folder->local = TRUE;
    folder->remote = FALSE;
    folder->item_id = -1;
    folder->sync_direction = ASK;
    if (strlen (folder->path) > 0) {
        updates = g_slist_prepend (updates, folder);
    }
    tmppath_gdir = g_strdup (g_dir_read_name (dir));
    while (tmppath_gdir != NULL) {
        tmppath = g_filename_to_utf8 (tmppath_gdir, -1, NULL, NULL, NULL);
        g_free (tmppath_gdir);
        if (strncmp (tmppath, ".", 1) != 0) {
            gchar *new_tmppath;
            new_tmppath = g_strconcat (path, "/", tmppath, NULL);
            g_free (tmppath);
            tmppath = new_tmppath;
            new_tmppath = NULL;
            gchar *localpath;
            localpath = g_strconcat (basepath, "/", tmppath, NULL);
            if (g_file_test (localpath, G_FILE_TEST_IS_DIR)) {
                scan_tree (tmppath, prefix, basepath);
            } else {
                if (strlen (tmppath) > 0) {
                    struct stat st;
                    lstat (localpath, &st);
                    struct object *file = g_malloc0 (sizeof (struct object));
                    file->type = FILE_TYPE;
                    file->path = g_strconcat (prefix, tmppath, NULL);
                    file->local_size = (int) st.st_size;
                    file->local = TRUE;
                    file->remote = FALSE;
                    file->sync_direction = ASK;
                    file->item_id = -1;
                    updates = g_slist_prepend (updates, file);
                }
            }
            g_free (localpath);
        }
        g_free (tmppath);
        tmppath_gdir = g_strdup (g_dir_read_name (dir));
    }
    g_free (tmppath_gdir);
    g_dir_close (dir);
    return;
}

/* Find the local path that is the equivelant of the device base path */
gchar *
find_base (gchar * path)
{
    gchar **fields;
    fields = g_strsplit (path, "/", 2);
    int base_id;
    LIBMTP_folder_t *folder;
    folder = folders;
    gchar *base_path;
    base_path = g_strconcat ("/", fields[0], NULL);
    base_id = lookup_folder_id (folder, base_path, "");
    g_free (base_path);
    g_strfreev (fields);
    if (base_id == device->default_music_folder) {
        return musicpath;
    } else if (base_id == device->default_playlist_folder) {
        return playpath;
    } else if (base_id == device->default_picture_folder) {
        return picturepath;
    } else if (base_id == device->default_video_folder) {
        return videopath;
    } else if (base_id == device->default_organizer_folder) {
        return organizerpath;
    } else if (base_id == device->default_zencast_folder) {
        return zencastpath;
    } else {
        return NULL;
    }
}

/* Scan device for folders and add to 'updates' list */
void
build_folderlist (LIBMTP_folder_t * folder, gchar * path)
{
    while (folder != NULL) {
        struct object *file = g_malloc0 (sizeof (struct object));
        file->type = FOLDER_TYPE;
        if (strlen (path) == 0) {
            file->path = g_strdup (folder->name);
        } else {
            file->path = g_strconcat (path, "/", folder->name, NULL);
        }
        file->remote_size = 0;
        file->remote = TRUE;
        file->local = FALSE;
        file->sync_direction = ASK;
        file->item_id = folder->folder_id;
        if (folder->child != NULL) {
            build_folderlist (folder->child, file->path);
        }

        if (strlen (file->path) > 0) {
            GSList *le;
            le = g_slist_find_custom (updates, file, compare_path);
            if (le != NULL) {
                struct object *item;
                item = le->data;
                item->remote = TRUE;
                item->remote_size = 0;
                item->item_id = folder->folder_id;
                g_free (file->path);
                g_free (file);
            } else {
                if (find_base (file->path) != NULL) {
                    updates = g_slist_prepend (updates, file);
                } else {
                    g_free (file->path);
                    g_free (file);
                }
            }
        }
        folder = folder->sibling;
    }
}

/* Compare two struct objects based on item_id */
static gint
compare_id (gconstpointer a, gconstpointer b)
{
    const struct object *x = a;
    const struct object *y = b;
    return x->item_id - y->item_id;
}

/* Compare two struct objects based on path */
static gint
compare_path (gconstpointer a, gconstpointer b)
{
    const struct object *x = a;
    const struct object *y = b;

    return g_strcasecmp (x->path, y->path);
}

/* See if item needs action
 * returns true if action required */
static gint
needs_action (gconstpointer a, gconstpointer b)
{
    const struct object *x = a;
    if (x->sync_direction != IGNORE ) {
        return FALSE;
    }
    return TRUE;
}

/* Scan device for files and add to 'updates' list */
void
build_filelist ()
{
    LIBMTP_file_t *file;
    file = files;
    while (file != NULL) {
        if (file->parent_id != device->default_playlist_folder) {
            struct object *item;
            struct object *tmpfile = g_malloc0 (sizeof (struct object));
            char *extension = rindex(file->filename,'.');
            if ((strncmp (file->filename, ".", 1) != 0) && ((extension != NULL) && (strcasecmp(extension,".alb") != 0))) {
                tmpfile->item_id = file->parent_id;
                tmpfile->type = FILE_TYPE;
                tmpfile->remote_size = file->filesize;
                tmpfile->remote = TRUE;
                tmpfile->local = FALSE;
                tmpfile->sync_direction = ASK;
                if (file->parent_id == 0) {
                    tmpfile->path = g_strconcat ("/", file->filename, NULL);
                } else {
                    GSList *le = NULL;
                    if (updates != NULL)
                        le = g_slist_find_custom (updates, tmpfile,
                                                  compare_id);
                    if (le != NULL) {
                        item = le->data;
                        tmpfile->path =
                          g_strconcat (item->path, "/", file->filename, NULL);
                    } else {
                        tmpfile->path =
                          g_strconcat ("/", file->filename, NULL);
                    }
                }
                GSList *le;
                le = g_slist_find_custom (updates, tmpfile, compare_path);
                if (le != NULL) {
                    item = le->data;
                    if (item->remote) {
                        tmpfile->item_id = file->item_id;
                        if (file->parent_id != 0) {
                            if (find_base (tmpfile->path) != NULL) {
                                updates = g_slist_prepend (updates, tmpfile);
                            } else {
                                g_free (tmpfile->path);
                                g_free (tmpfile);
                            }
                        }
                    } else {
                        item->remote = TRUE;
                        item->remote_size = file->filesize;
                        item->item_id = file->item_id;
                        g_free (tmpfile->path);
                        g_free (tmpfile);
                    }
                } else {
                    tmpfile->item_id = file->item_id;
                    if (file->parent_id != 0) {
                        if (find_base (tmpfile->path) != NULL) {
                            updates = g_slist_prepend (updates, tmpfile);
                        } else {
                            g_free (tmpfile->path);
                            g_free (tmpfile);
                        }
                    } else {
                        g_free (tmpfile->path);
                        g_free (tmpfile);
                    }
                }
            }
        }
        file = file->next;
    }
}

/* Do any pending gtk events */
void
do_pending ()
{
    if (!nogui) {
        while (gtk_events_pending ())
            gtk_main_iteration ();
    }
}

/* Display a message to the user
 * Outputs to statusbar if running gui, otherwise prints to stdout */
void
display_message (const gchar * message)
{
    if (nogui) {
        g_print ("%s\n", message);
    } else {
        GtkWidget *statusbar =
          lookup_widget (GTK_WIDGET (windowMain), "statusbar");
        gtk_widget_show (windowMain);
        gtk_statusbar_pop (GTK_STATUSBAR (statusbar), context_id);
        gtk_statusbar_push (GTK_STATUSBAR (statusbar), context_id, message);
        if (progressbar != NULL) {
            gtk_progress_bar_set_text(progressbar,message);
        }
        do_pending ();
    }
}

/* Scan a device and look for folders, files and playlists */
void
scan_device ()
{
    LIBMTP_folder_t *folder;
    folder = folders;
    display_message ("Building Music Folderlist");
    build_folderlist (folder, "");
    display_message ("Building Filelist");
    build_filelist ();
    if (playpath != NULL) {     /* Only look for playlists if needed */
        display_message ("Building Playlists");
        scan_playlists ();
    }
    display_message ("Done");
}

/* Strip of the last part of a path in order to find it's parent
 * Returns new string */
gchar *
find_parent (gchar * path)
{
    gchar **fields;
    gchar *parent;
    fields = g_strsplit (path, "/", -1);
    fields[g_strv_length (fields)] = NULL;
    parent = g_strjoinv ("/", fields);
    g_strfreev (fields);
    return parent;
}

/* Check item to see if update needed
 * If so check if change already forced, otherwise ask user */
void
confirm_item (struct object *item)
{
    char *userinput;
    struct object *file = g_malloc0 (sizeof (struct object));
    GSList *le;

    /* find parent of item and if it is in the updates use it's direction */

    file->path = find_parent (item->path);
    le = g_slist_find_custom (updates, file, compare_path);
    g_free (file->path);
    g_free (file);
    if (le != NULL) {
        struct object *item2;
        item2 = le->data;
        if (item2->sync_direction != ASK) {
            item->sync_direction = item2->sync_direction;
            return;
        }
    }

    item->sync_direction = IGNORE;
    if (item->remote && item->local) {
        /* need to special case playlists */
        if (item->local_size != item->remote_size) {
            if (item->local_size > item->remote_size) {
                g_printf ("%s is smaller on device", item->path);
            } else {
                g_printf ("%s is larger on device", item->path);
            }
            g_printf (" size:%d,%d\n", item->local_size, item->remote_size);
        } else {
            return;
        }
    } else if (item->remote) {
        g_printf ("%s only exists on device", item->path);
    } else if (item->local) {
        g_printf ("%s doesn't exist on device", item->path);
    } else {
        g_printf ("%s is in error\n", item->path);
        item->sync_direction = IGNORE;
        return;
    }
    if (force_direction) {
        item->sync_direction = force_direction;
        g_printf ("\n");
        return;
    }
    userinput = readline (":");
    if (strncmp (userinput, ".", 1) == 0) {
        item->sync_direction = LOCAL_TO_REMOTE;
    } else if (strncmp (userinput, ",", 1) == 0) {
        item->sync_direction = REMOTE_TO_LOCAL;
    } else if (strncmp (userinput, ">", 1) == 0) {
        item->sync_direction = LOCAL_TO_REMOTE;
        force_direction = LOCAL_TO_REMOTE;
    } else if (strncmp (userinput, "<", 1) == 0) {
        item->sync_direction = REMOTE_TO_LOCAL;
        force_direction = REMOTE_TO_LOCAL;
    }
    g_free (userinput);
}

/* Send a single file to the device
 * Sends mp3 files as tracks, all other as simple files
 * Sets the filetype on the device based on file extension */
int
send_file (struct object *item)
{
    gchar *message;
    message = g_strdup_printf ("Sending %s", item->path);
    display_message (message);
    g_free (message);
    /* find parent id */
    gchar *filename;
    gchar **fields;
    gchar *directory;
    gchar *localpath;
    fields = g_strsplit (item->path, "/", -1);
    fields[0] = g_strdup (find_base (item->path));
    if (fields[0] == NULL) {
        g_printf ("Skipping\n");
        g_strfreev (fields);
        return;
    }

    localpath = g_strjoinv ("/", fields);

    if (playpath != NULL) {
        if (strcmp (fields[0], playpath) == 0) {
            int ret = save_playlist (localpath);
            if (ret != 0)
                g_debug ("Unable to send playlist %s", item->path);
            /* Don't remove remote later as send_items will do it for us */
            item->remote = FALSE;
            g_strfreev (fields);
            return;
        }
    }
    g_strfreev (fields);

    /* Make sure directory (and all parents thereof) exist
     * if any do not, create them */
    directory = g_strconcat ("/", NULL);
    fields = g_strsplit (item->path, "/", -1);
    int i;
    uint32_t parent_id = 0;
    for (i = 0; fields[i] != NULL; i++) {
        if (strlen (fields[i]) > 0) {
            if (fields[i + 1] == NULL) {
                directory = g_strndup (directory, strlen (directory) - 1);
                if (folders_changed) {
                    folders = LIBMTP_Get_Folder_List (device);
                    folders_changed = FALSE;
                }
                int my_id = lookup_folder_id (folders, directory, "");
                if (my_id < 0) {
                    /* The parent folder was not made correctly so remove
                    * failed one*/
                    g_printf("Directory creation failed\n");
                    LIBMTP_Delete_Object (device, parent_id);
                    return -1;
                }
                parent_id = (uint32_t) my_id;
                filename = g_strdup (fields[i]);
            } else {
                directory = g_strconcat (directory, fields[i], NULL);
                if (folders_changed) {
                    folders = LIBMTP_Get_Folder_List (device);
                    folders_changed = FALSE;
                }
                int my_id = lookup_folder_id (folders, directory, "");
                if (my_id < 0) {
                    g_printf ("Make the directory:%s:%s:%d\n", fields[i],
                              directory, parent_id);
                    my_id =
                      LIBMTP_Create_Folder (device, fields[i],
                                            (uint32_t) parent_id);
                    folders_changed = TRUE;
                }
                parent_id = (uint32_t) my_id;
                directory = g_strconcat (directory, "/", NULL);
            }
        }
    }
    g_strfreev (fields);


    /* Open file and get details */
    struct stat st;
    uint64_t filesize;
    int fd = open (localpath, O_RDONLY);
    fstat (fd, &st);
    if (S_ISREG (st.st_mode)) {
        filesize = (uint64_t) st.st_size;

        LIBMTP_filetype_t filetype;
        filetype = find_filetype (filename);
        int ret;
        if (filetype == LIBMTP_FILETYPE_MP3) {
            /* Find track details (ie id3 tags) and then send as track */
            LIBMTP_track_t *genfile;
            genfile = LIBMTP_new_track_t ();
            gint songlen;
            struct id3_file *id3_fh;
            struct id3_tag *tag;
            gchar *tracknum;


            id3_fh = id3_file_fdopen (fd, ID3_FILE_MODE_READONLY);
            tag = id3_file_tag (id3_fh);

            genfile->artist = getArtist (tag);
            genfile->title = getTitle (tag);
            genfile->album = getAlbum (tag);
            genfile->genre = getGenre (tag);
            genfile->date = getYear (tag);
            genfile->usecount = 0;

            /* If there is a songlength tag it will take
             * precedence over any length calculated from
             * the bitrate and filesize */
            songlen = getSonglen (tag);
            if (songlen > 0) {
                genfile->duration = songlen * 1000;
            } else {
                int fd;
                genfile->duration = (uint16_t) calc_length (fd) * 1000;
            }
            genfile->tracknumber = getTracknum (tag);

            /* Compensate for missing tag information */
            if (!genfile->artist)
                genfile->artist = g_strdup ("<Unknown>");
            if (!genfile->title)
                genfile->title = g_strdup ("<Unknown>");
            if (!genfile->album)
                genfile->album = g_strdup ("<Unknown>");
            if (!genfile->genre)
                genfile->genre = g_strdup ("<Unknown>");

            genfile->filesize = filesize;
            genfile->filetype = filetype;
            genfile->filename = g_strdup (filename);
            //g_debug("%d:%d:%d",fd,genfile->duration,genfile->filesize);
            ret =
              LIBMTP_Send_Track_From_File_Descriptor (device, fd, genfile,
                                                      NULL, NULL, parent_id);
            id3_file_close (id3_fh);
            close (fd);
            LIBMTP_destroy_track_t (genfile);
        } else {
            /* Send as a file */
            LIBMTP_file_t *genfile;
            genfile = LIBMTP_new_file_t ();
            genfile->filesize = filesize;
            genfile->filetype = filetype;
            genfile->filename = g_strdup (filename);
            genfile->parent_id = parent_id;

            //g_debug("Send %s parent %d",localpath,parent_id);
            ret =
              LIBMTP_Send_File_From_File_Descriptor (device, fd, genfile,
                                                     NULL, NULL, parent_id);
            LIBMTP_destroy_file_t (genfile);
        }
        if (ret != 0)
            g_debug ("Unable to send %s", item->path);
        return ret;
    }
    return -1;
}

/* Get a file from the remote device
 * Details come from item passed */
int
get_file (struct object *item)
{
    g_printf ("Getting %s\n", item->path);
    gchar *local_path;
    gchar **fields;
    gchar *localpath;
    gchar *orig_field;
    int i;
    struct stat dstat;
 
    fields = g_strsplit (item->path, "/", -1);
    fields[0] = g_strdup (find_base (item->path));
    if (fields[0] == NULL) {
        display_message ("Skipping\n");
        return;
    }

    // make items parent dir with parents
    i = 1; 
    while (fields[i] != NULL){
      // save field
      orig_field = fields[i];
      fields[i] = NULL;
      localpath = g_strjoinv ("/", fields);
      // mkdir if file doesnt exist
      if (stat(localpath, &dstat)) {
	if (mkdir(localpath,  S_IRUSR | S_IWUSR | S_IXUSR | 
		  S_IRGRP | S_IXGRP | 
		  S_IROTH | S_IXOTH) == -1) {
	  display_message(strerror(errno));
	  fields[i] = orig_field;
	  break;
	} 
      }
      //	restore field;
      fields[i] = orig_field;
      i++;
    }
    
    localpath = g_strjoinv ("/", fields);

    int ret;
    if (item->type == FILE_TYPE) {
        ret =
          LIBMTP_Get_File_To_File (device, item->item_id, localpath, NULL,
                                   NULL);
    } else {
        ret = mkdir (localpath, 0777);
    }
    return ret;
}

/* Remote a item from the device */
int
delete_remote_file (struct object *item)
{
    gchar *message;
    message = g_strdup_printf ("Deleting %s from device", item->path);
    display_message (message);
    g_free (message);
    int ret = LIBMTP_Delete_Object (device, item->item_id);
    return ret;
}

/* Remove a file from the local filesystem */
int
delete_local_file (struct object *item)
{
    g_printf ("deleting %s from local\n", item->path);
    gchar **fields;
    gchar *localpath;
    fields = g_strsplit (item->path, "/", -1);
    fields[0] = g_strdup (find_base (item->path));
    if (fields[0] == NULL) {
        g_printf ("Skipping\n");
        g_strfreev (fields);
        return 1;
    }

    localpath = g_strjoinv ("/", fields);
    int ret = unlink (localpath);
    return ret;
}

/* Send/Receive changed files */
void
send_items (struct object *item)
{
    int ret_add = -1;
    int ret_del = -1;
    if (item->sync_direction == LOCAL_TO_REMOTE) {
        if (item->local) {
            ret_add = send_file (item);
        } else {
            ret_add = 0;
        }
        if ((item->remote) && (item->type == FILE_TYPE)) {
            ret_del = delete_remote_file (item);
        } else if (item->type == FILE_TYPE) {
            ret_del = 0;
        } 
    } else if (item->sync_direction == REMOTE_TO_LOCAL) {
        if (item->remote) {
            ret_add = get_file (item);
        } else {
            ret_add = 0;
        }
        if ((item->local) && (item->type == FILE_TYPE)) {
            ret_del = delete_local_file (item);
        } else if (item->type == FILE_TYPE) {
            ret_del = 0;
        }
    }
    if ( (ret_add == 0) && (ret_del == 0) ) {
        item->sync_direction = IGNORE;
    }
}

/* Remove folders from filesystem and device */
void
delete_items (struct object *item)
{
    int ret = -1;
    if (item->sync_direction == LOCAL_TO_REMOTE) {
        if (item->remote && (item->type != FILE_TYPE)) {
            ret = delete_remote_file (item);
        }
    } else if (item->sync_direction == REMOTE_TO_LOCAL) {
        if (item->local && !item->remote && (item->type != FILE_TYPE)) {
            ret = delete_local_file (item);
        }
    }
    if (ret == 0) {
        item->sync_direction = IGNORE;
    }
}

/* Find the item_id of a given path */
int
parse_path (gchar * path)
{
    int item_id = -1;
    int i;
    //g_print ("parse_path:%s\n",path);

    /* Check device */
    LIBMTP_folder_t *folder;
    gchar **fields;
    gchar *directory;
    gchar *file;
    directory = (gchar *) g_malloc0 (strlen (path));
    directory = strcpy (directory, "");
    fields = g_strsplit (path, "/", -1);
    for (i = 0; fields[i] != NULL; i++) {
        if (strlen (fields[i]) > 0) {
            if (fields[i + 1] != NULL) {
                directory = strcat (directory, "/");
                directory = strcat (directory, fields[i]);
            } else {
                folder = folders;
                int folder_id = 0;
                if (strcmp (directory, "") != 0) {
                    folder_id = lookup_folder_id (folder, directory, "");
                }
                //g_debug ("parent id:%d:%s", folder_id, directory);
                LIBMTP_file_t *file, *tmp;
                file = files;
                while (file != NULL) {
                    if (file->parent_id == folder_id) {
                        if (strcasecmp (file->filename, fields[i]) == 0) {
                            //g_debug ("found:%d:%s", file->item_id, file->filename);
                            item_id = file->item_id;
                            g_strfreev (fields);
                            return item_id;
                        }
                    }
                    file = file->next;
                }
                if (item_id < 0) {
                    directory = strcat (directory, fields[i]);
                    item_id = lookup_folder_id (folder, directory, "");
                    //g_strfreev(fields);
                    return item_id;
                }
            }
        }
    }
    g_strfreev (fields);
    return -ENOENT;
}

/* Save a playlist from a m3u file to the device */
int
save_playlist (const gchar *path)
{
    int ret = 0;
    LIBMTP_playlist_t *playlist;
    FILE *file = NULL;
    gchar item_path[1024];
    uint32_t item_id = 0;
    int no_tracks = 0;
    uint32_t *tracks;
    gchar **fields;
    GSList *tmplist = NULL;

    fields = g_strsplit (path, "/", -1);
    int field_no = g_strv_length (fields) - 1;
    gchar *playlist_name;
    playlist_name =
      g_strndup (fields[field_no], strlen (fields[field_no]) - 4);
    //g_debug("Adding:%s",playlist_name);
    g_strfreev (fields);

    playlist = LIBMTP_new_playlist_t ();
    playlist->name = g_strdup (playlist_name);

    /* Find base path */
    LIBMTP_folder_t *folder;
    folder = LIBMTP_Find_Folder (folders, device->default_music_folder);
    file = fopen (path, "r");
    if (file == NULL) {
        g_printf ("Unable to open %s\n", path);
        return;
    }
    while (fgets (item_path, sizeof (item_path) - 1, file) != NULL) {
        g_strchomp (item_path);
        gchar *item_fullpath;
        item_fullpath = g_strdup_printf("/%s/%s",folder->name, item_path) ;
        item_id = parse_path (item_fullpath);
        g_free(item_fullpath);
        if (item_id != -1) {
            tmplist = g_slist_append (tmplist, GUINT_TO_POINTER (item_id));
            //g_debug("Adding to tmplist:%d",item_id);
        }
    }
    playlist->no_tracks = g_slist_length (tmplist);
    tracks = g_malloc0 (playlist->no_tracks * sizeof (uint32_t));
    int i;
    for (i = 0; i < playlist->no_tracks; i++) {
        tracks[i] =
          (uint32_t) GPOINTER_TO_UINT (g_slist_nth_data (tmplist, i));
        //g_debug("Adding:%d-%d",i,tracks[i]);
    }
    playlist->tracks = tracks;
    //g_debug("Total:%d",playlist->no_tracks);

    int playlist_id = 0;
    LIBMTP_playlist_t *tmp_playlist;
    tmp_playlist = playlists;
    while (tmp_playlist != NULL) {
        if (strcasecmp (tmp_playlist->name, playlist_name) == 0) {
            playlist_id = tmp_playlist->playlist_id;
        }
        tmp_playlist = tmp_playlist->next;
    }

    if (playlist_id > 0) {
        //g_debug("Update playlist %d",playlist_id);
        playlist->playlist_id = playlist_id;
        ret = LIBMTP_Update_Playlist (device, playlist);
        ret = 0;
    } else {
        //g_debug("New playlist");
        ret = LIBMTP_Create_New_Playlist (device, playlist, 0);
        ret = 0;
    }
    return ret;
}

int progressfunc(uint64_t const sent, uint64_t const total,
		 void const * const data) {
  //  g_debug("sent %d of %d\n", sent, total);
}

/* Refresh list of updates */
int
refresh_list ()
{
    if (device == NULL) {
        LIBMTP_Init ();
        device = LIBMTP_Get_First_Device ();
        if (device == NULL) {
            display_message ("No devices.");
            return (1);
        }
    }
    /* Load details from device */
    display_message ("Getting Filelist");
    files = LIBMTP_Get_Filelisting_With_Callback (device,NULL,NULL);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.05);
    display_message ("Getting Folderlist");
    folders = LIBMTP_Get_Folder_List (device);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.10);
    if (playpath != NULL) {
        display_message ("Getting Playlists");
        playlists = LIBMTP_Get_Playlist_List (device);
    }
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.15);

    g_slist_free (updates);
    updates = NULL;
    /* Scan for Music */
    LIBMTP_folder_t *folder;
    folder = LIBMTP_Find_Folder (folders, device->default_music_folder);
    if (folder)
      scan_tree ("", folder->name, musicpath);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.20);
    /* Scan for Playlists */
    folder = LIBMTP_Find_Folder (folders, device->default_playlist_folder);
    if (folder)
      scan_tree ("", folder->name, playpath);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.25);
    /* Scan for Pictures */
    folder = LIBMTP_Find_Folder (folders, device->default_picture_folder);
    if (folder)
      scan_tree ("", folder->name, picturepath);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.30);
    /* Scan for Videos */
    folder = LIBMTP_Find_Folder (folders, device->default_video_folder);
    if (folder)
      scan_tree ("", folder->name, videopath);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.35);
    /* Scan for Organizer items */
    folder = LIBMTP_Find_Folder (folders, device->default_organizer_folder);
    if (folder)
      scan_tree ("", folder->name, organizerpath);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.40);
    /* Scan for Zencast */
    folder = LIBMTP_Find_Folder (folders, device->default_zencast_folder);
    if (folder)
      scan_tree ("", folder->name, zencastpath);
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.45);

    /* Scan device */
    scan_device ();
    if (progressbar != NULL) gtk_progress_bar_set_fraction(progressbar,0.50);

    updates = g_slist_sort (updates, compare_path);
    return 0;
}

/* Read configuration from gconfd */
void
read_gconf ()
{
    gconf = gconf_client_get_default ();
    gconf_client_add_dir (gconf, "/apps/mtpsync",
                          GCONF_CLIENT_PRELOAD_RECURSIVE, NULL);

    GConfValue *gv = NULL;
    musicpath = NULL;
    playpath = NULL;
    picturepath = NULL;
    videopath = NULL;
    organizerpath = NULL;
    zencastpath = NULL;

    gv = gconf_client_get (gconf, "/apps/mtpsync/music_path", NULL);
    if (gconf_client_get_bool (gconf, "/apps/mtpsync/sync_music", NULL)
        && (gv != NULL)) {
        musicpath = g_strdup (gconf_value_get_string (gv));
    }
    gv = gconf_client_get (gconf, "/apps/mtpsync/playlist_path", NULL);
    if (gconf_client_get_bool (gconf, "/apps/mtpsync/sync_playlist", NULL)
        && (gv != NULL)) {
        playpath = g_strdup (gconf_value_get_string (gv));
    }
    gv = gconf_client_get (gconf, "/apps/mtpsync/image_path", NULL);
    if (gconf_client_get_bool (gconf, "/apps/mtpsync/sync_image", NULL)
        && (gv != NULL)) {
        picturepath = g_strdup (gconf_value_get_string (gv));
    }
    gv = gconf_client_get (gconf, "/apps/mtpsync/video_path", NULL);
    if (gconf_client_get_bool (gconf, "/apps/mtpsync/sync_video", NULL)
        && (gv != NULL)) {
        videopath = g_strdup (gconf_value_get_string (gv));
    }
    gv = gconf_client_get (gconf, "/apps/mtpsync/organizer_path", NULL);
    if (gconf_client_get_bool (gconf, "/apps/mtpsync/sync_organizer", NULL)
        && (gv != NULL)) {
        organizerpath = g_strdup (gconf_value_get_string (gv));
    }
    gv = gconf_client_get (gconf, "/apps/mtpsync/zencast_path", NULL);
    if (gconf_client_get_bool (gconf, "/apps/mtpsync/sync_zencast", NULL)
        && (gv != NULL)) {
        zencastpath = g_strdup (gconf_value_get_string (gv));
    }
    GtkWidget *buttonUpdate = lookup_widget (GTK_WIDGET (windowMain), "buttonUpdate");
    if ((musicpath != NULL) || (playpath  != NULL) || (picturepath != NULL) || (videopath != NULL) || (organizerpath != NULL) || (zencastpath != NULL)) {
        gtk_tool_button_set_label(GTK_TOOL_BUTTON(buttonUpdate),"Update Player");
        gtk_tool_button_set_icon_name(GTK_TOOL_BUTTON(buttonUpdate),"gtk-media-play");
    } else {
        gtk_tool_button_set_label(GTK_TOOL_BUTTON(buttonUpdate),"Setup");
    }
}

/* Write configuration to gconfd */
void
write_gconf ()
{
    /* Music */
    GtkWidget *file = lookup_widget (GTK_WIDGET (dialogSetup), "fileMusic");
    GtkWidget *check = lookup_widget (GTK_WIDGET (dialogSetup), "checkMusic");
    gconf_client_set_bool (gconf, "/apps/mtpsync/sync_music",
                           gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                         (check)), NULL);
    gconf_client_set_string (gconf, "/apps/mtpsync/music_path",
                             gtk_file_chooser_get_filename (GTK_FILE_CHOOSER
                                                            (file)), NULL);

    /* Playlists */
    file = lookup_widget (GTK_WIDGET (dialogSetup), "filePlaylists");
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkPlaylists");
    gconf_client_set_bool (gconf, "/apps/mtpsync/sync_playlist",
                           gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                         (check)), NULL);
    gconf_client_set_string (gconf, "/apps/mtpsync/playlist_path",
                             gtk_file_chooser_get_filename (GTK_FILE_CHOOSER
                                                            (file)), NULL);

    /* Images */
    file = lookup_widget (GTK_WIDGET (dialogSetup), "fileImages");
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkImages");
    gconf_client_set_bool (gconf, "/apps/mtpsync/sync_image",
                           gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                         (check)), NULL);
    gconf_client_set_string (gconf, "/apps/mtpsync/image_path",
                             gtk_file_chooser_get_filename (GTK_FILE_CHOOSER
                                                            (file)), NULL);

    /* Videos */
    file = lookup_widget (GTK_WIDGET (dialogSetup), "fileVideo");
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkVideos");
    gconf_client_set_bool (gconf, "/apps/mtpsync/sync_video",
                           gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                         (check)), NULL);
    gconf_client_set_string (gconf, "/apps/mtpsync/video_path",
                             gtk_file_chooser_get_filename (GTK_FILE_CHOOSER
                                                            (file)), NULL);

    /* Zencasts */
    file = lookup_widget (GTK_WIDGET (dialogSetup), "fileZencast");
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkZencasts");
    gconf_client_set_bool (gconf, "/apps/mtpsync/sync_zencast",
                           gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                         (check)), NULL);
    gconf_client_set_string (gconf, "/apps/mtpsync/zencast_path",
                             gtk_file_chooser_get_filename (GTK_FILE_CHOOSER
                                                            (file)), NULL);

    /* Organizer */
    file = lookup_widget (GTK_WIDGET (dialogSetup), "fileOrganizer");
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkOrganizer");
    gconf_client_set_bool (gconf, "/apps/mtpsync/sync_organizer",
                           gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON
                                                         (check)), NULL);
    gconf_client_set_string (gconf, "/apps/mtpsync/organizer_path",
                             gtk_file_chooser_get_filename (GTK_FILE_CHOOSER
                                                            (file)), NULL);
}

/* Main program where all the work begins */
int
main (int argc, char *argv[])
{

#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    GOptionContext *context;
    context = g_option_context_new ("- Syncronise MTP device");
    g_option_context_add_main_entries (context, entries, NULL);
    g_option_context_add_group (context, gtk_get_option_group (TRUE));
    g_option_context_parse (context, &argc, &argv, NULL);
    g_option_context_free (context);

    if (!nogui) {
        gtk_set_locale ();
        gtk_init (&argc, &argv);
        g_type_init ();
        add_pixmap_directory (PACKAGE_DATA_DIR "/" PACKAGE "/pixmaps");
        windowMain = create_windowMain ();
        read_gconf ();
        GtkWidget *statusbar =
          lookup_widget (GTK_WIDGET (windowMain), "statusbar");
        gtk_widget_show (windowMain);
        context_id =
          gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar), "sync");


        /* Initialize table */
        store =
          gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
                              G_TYPE_INT, G_TYPE_STRING);
        GtkWidget *treeChanges =
          lookup_widget (GTK_WIDGET (windowMain), "treeChanges");
        gtk_tree_view_set_model (GTK_TREE_VIEW (treeChanges),
                                 GTK_TREE_MODEL (store));
        GtkCellRenderer *renderer;
        GtkTreeViewColumn *column;
        renderer = gtk_cell_renderer_text_new ();
        column =
          gtk_tree_view_column_new_with_attributes ("Action", renderer,
                                                    "text", 0, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeChanges), column);
        renderer = gtk_cell_renderer_text_new ();
        column =
          gtk_tree_view_column_new_with_attributes ("Item", renderer,
                                                    "text", 1, NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (treeChanges), column);
        gtk_widget_set_sensitive (treeChanges, TRUE);

        display_message ("Waiting for input");

        gtk_main ();
    } else {
        if ((musicpath == NULL) && (playpath == NULL) && (picturepath == NULL)
            && (videopath == NULL) && (organizerpath == NULL)
            && (zencastpath == NULL)) {
            display_message ("No paths specified. Exiting");
            exit (1);
        }
        int ret;
        ret = refresh_list ();
        if (ret) {
            exit (1);
        }
        display_message ("Confirming updates");
        g_slist_foreach (updates, (GFunc) confirm_item, NULL);
        display_message ("Go");
        gchar *userinput;
        userinput = readline ("?");
        if (strncmp (userinput, "y", 1) == 0) {
            int retries = 0;
            GSList *le;
            le = g_slist_find_custom (updates, NULL, needs_action);
            while (le != NULL) {
                display_message ("Sending files");
                g_slist_foreach (updates, (GFunc) send_items, NULL);
                updates = g_slist_reverse (updates);
                display_message ("Removing files");
                g_slist_foreach (updates, (GFunc) delete_items, NULL);

                le = g_slist_find_custom (updates, NULL, needs_action);
                if (le != NULL) {
                    LIBMTP_Release_Device (device);
                    retries++;
                    if (retries > 2) {
                        display_message("Unable to make all changes..exiting");
                        return (-1);   
                    }
                    display_message("Unable to make all changes..retrying");
                    device = LIBMTP_Get_First_Device ();
                    display_message ("Getting Filelist");
                    files = LIBMTP_Get_Filelisting_With_Callback (device,NULL,NULL);
                    display_message ("Getting Folderlist");
                    folders = LIBMTP_Get_Folder_List (device);
                    if (playpath != NULL) {
                        display_message ("Getting Playlists");
                        playlists = LIBMTP_Get_Playlist_List (device);
                    }
                }
            }
        }
        LIBMTP_Release_Device (device);
        display_message ("Finished");
    }

    return (0);
}

/* Update the table of differences in gui */
void
update_table (struct object *item)
{
    gchar *message = NULL;
    GSList *le;


    /* find parent of item and if it is in the updates use it's direction */
    if (item->remote && item->local) {
        if (item->local_size != item->remote_size) {
            if (item->local_size > item->remote_size) {
                message =
                  g_strdup_printf ("%s is smaller on device (%d vs %d)",
                                   item->path, item->local_size,
                                   item->remote_size);
            } else {
                message =
                  g_strdup_printf ("%s is larger on device (%d vs %d)",
                                   item->path, item->local_size,
                                   item->remote_size);
            }
        } else {
            return;
        }
    } else if (item->remote) {
        message = g_strdup_printf ("%s only exists on device", item->path);
    } else if (item->local) {
        message = g_strdup_printf ("%s doesn't exist on device", item->path);
    } else {
        message = g_strdup_printf ("%s is in error\n", item->path);
        item->sync_direction = IGNORE;
        return;
    }

    if (force_direction) {
        item->sync_direction = force_direction;
        return;
    }
    GtkTreeIter iter;

    gtk_list_store_append (store, &iter);   /* Acquire an iterator */
    gtk_list_store_set (store, &iter, ACTION_COLUMN, "?", MESSAGE_COLUMN,
                        message, ITEMID_COLUMN, item->item_id, PATH_COLUMN,
                        item->path, -1);
    g_free (message);
}

/* Change items in table based on given sync_direction */
void
change_table (int sync_direction)
{
    GtkWidget *treeChanges =
      lookup_widget (GTK_WIDGET (windowMain), "treeChanges");
    GtkTreeSelection *selection =
      gtk_tree_view_get_selection (GTK_TREE_VIEW (treeChanges));
    GtkTreeModel *model;
    GtkTreeIter iter;
    int ret = gtk_tree_selection_get_selected (selection, &model, &iter);
    if (ret) {
        int item_id = 0;
        gchar *path = NULL;
        gchar *action_val = NULL;
        gtk_tree_model_get (model, &iter, ACTION_COLUMN, &action_val,
                            PATH_COLUMN, &path, ITEMID_COLUMN, &item_id, -1);
        if (sync_direction == LOCAL_TO_REMOTE) {
            action_val = g_strdup (">");
        } else if (sync_direction == REMOTE_TO_LOCAL) {
            action_val = g_strdup ("<");
        } else if (sync_direction == IGNORE) {
            action_val = g_strdup ("?");
        }
        /* Update item */
        GSList *le;
        struct object *file = g_malloc0 (sizeof (struct object));
        if (item_id > 0) {
            file->item_id = item_id;
            le = g_slist_find_custom (updates, file, compare_id);
        } else {
            file->path = path;
            le = g_slist_find_custom (updates, file, compare_path);
        }
        struct object *item;
        item = le->data;
        item->sync_direction = sync_direction;
        /* Change table */
        gtk_list_store_set (store, &iter, ACTION_COLUMN, action_val, -1);
        gtk_tree_selection_unselect_iter (selection, &iter);
        gtk_tree_model_iter_next (model, &iter);
        gtk_tree_selection_select_iter (selection, &iter);
        if (force_direction != ASK) {
            change_table (sync_direction);
        }
    }
}
