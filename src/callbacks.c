#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>

#include "main.h"
#include "callbacks.h"
#include "interface.h"
#include "support.h"


/* User has asked syncronisation to begin */
void
on_buttonSync_clicked (GtkToolButton * toolbutton, gpointer user_data)
{
    display_message ("Sending files");
    g_slist_foreach (updates, (GFunc) send_items, NULL);
    updates = g_slist_reverse (updates);
    display_message ("Removing files");
    g_slist_foreach (updates, (GFunc) delete_items, NULL);
    display_message ("Done");
}

/* Load setup dialog */
void
on_buttonSetup_clicked (GtkToolButton * toolbutton, gpointer user_data)
{
    dialogSetup = create_dialogSetup ();
    GConfValue *gv = NULL;

    // Setup dialog based on values in gconf
    // Can't use 'read_gconf' function as we need ALL data
    gv = gconf_client_get (gconf, "/apps/mtpsync/music_path", NULL);
    if (gv != NULL) {
	GtkWidget *file =
	    lookup_widget (GTK_WIDGET (dialogSetup), "fileMusic");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file),
					     gconf_value_get_string (gv));
    }
    GtkWidget *check = lookup_widget (GTK_WIDGET (dialogSetup), "checkMusic");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				  gconf_client_get_bool (gconf,
							 "/apps/mtpsync/sync_music",
							 NULL));

    gv = gconf_client_get (gconf, "/apps/mtpsync/playlist_path", NULL);
    if (gv != NULL) {
	GtkWidget *file =
	    lookup_widget (GTK_WIDGET (dialogSetup), "filePlaylists");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file),
					     gconf_value_get_string (gv));
    }
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkPlaylists");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				  gconf_client_get_bool (gconf,
							 "/apps/mtpsync/sync_playlist",
							 NULL));

    gv = gconf_client_get (gconf, "/apps/mtpsync/image_path", NULL);
    if (gv != NULL) {
	GtkWidget *file =
	    lookup_widget (GTK_WIDGET (dialogSetup), "fileImages");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file),
					     gconf_value_get_string (gv));
    }
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkImages");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				  gconf_client_get_bool (gconf,
							 "/apps/mtpsync/sync_image",
							 NULL));

    gv = gconf_client_get (gconf, "/apps/mtpsync/video_path", NULL);
    if (gv != NULL) {
	GtkWidget *file =
	    lookup_widget (GTK_WIDGET (dialogSetup), "fileVideo");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file),
					     gconf_value_get_string (gv));
    }
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkVideos");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				  gconf_client_get_bool (gconf,
							 "/apps/mtpsync/sync_video",
							 NULL));

    gv = gconf_client_get (gconf, "/apps/mtpsync/zencast_path", NULL);
    if (gv != NULL) {
	GtkWidget *file =
	    lookup_widget (GTK_WIDGET (dialogSetup), "fileZencast");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file),
					     gconf_value_get_string (gv));
    }
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkZencasts");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				  gconf_client_get_bool (gconf,
							 "/apps/mtpsync/sync_zencast",
							 NULL));

    gv = gconf_client_get (gconf, "/apps/mtpsync/organizer_path", NULL);
    if (gv != NULL) {
	GtkWidget *file =
	    lookup_widget (GTK_WIDGET (dialogSetup), "fileOrganizer");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file),
					     gconf_value_get_string (gv));
    }
    check = lookup_widget (GTK_WIDGET (dialogSetup), "checkOrganizer");
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check),
				  gconf_client_get_bool (gconf,
							 "/apps/mtpsync/sync_organizer",
							 NULL));


    gint result = gtk_dialog_run (GTK_DIALOG (dialogSetup));
    if (result == GTK_RESPONSE_OK) {
	    write_gconf ();
	    read_gconf ();
    }
    gtk_widget_destroy (dialogSetup);
}

/* Refresh updates table */
void
on_buttonRefresh_clicked (GtkToolButton * toolbutton, gpointer user_data)
{
    gtk_list_store_clear (store);
    int ret = refresh_list ();
    if (ret==0) {
        g_slist_foreach (updates, (GFunc) update_table, NULL);
        GtkWidget *treeChanges =
        lookup_widget (GTK_WIDGET (windowMain), "treeChanges");
        GtkTreeSelection *selection =
        gtk_tree_view_get_selection (GTK_TREE_VIEW (treeChanges));
        GtkTreeIter iter;
        GtkTreeModel *model;
        model = gtk_tree_view_get_model(GTK_TREE_VIEW (treeChanges));
        gtk_tree_model_get_iter_first(model,&iter);
        gtk_tree_selection_select_iter (selection, &iter);
    } else {
        display_message("No device found");
    }
}

/* Release device and quit */
void
on_buttonQuit_clicked (GtkToolButton * toolbutton, gpointer user_data)
{
    if (device != NULL) {
	LIBMTP_Release_Device (device);
    }
    gtk_main_quit ();
}

/* Force rest of changes from Remote to Local */
void
on_buttonAllRemote_clicked (GtkButton * button, gpointer user_data)
{
    force_direction = REMOTE_TO_LOCAL;
    change_table (REMOTE_TO_LOCAL);
}


/* Make change from Remote to Local */
void
on_buttonRemote_clicked (GtkButton * button, gpointer user_data)
{
    change_table (REMOTE_TO_LOCAL);
}


/* Ignore this item */
void
on_buttonSkip_clicked (GtkButton * button, gpointer user_data)
{
    change_table (IGNORE);
}


/* Make Change from Local to Remote */
void
on_buttonLocal_clicked (GtkButton * button, gpointer user_data)
{
    change_table (LOCAL_TO_REMOTE);
}


/* Force rest of changes from Local to Remote */
void
on_buttonAllLocal_clicked (GtkButton * button, gpointer user_data)
{
    force_direction = LOCAL_TO_REMOTE;
    change_table (LOCAL_TO_REMOTE);
}

void
on_buttonUpdate_clicked                (GtkToolButton   *toolbutton,
                                        gpointer         user_data)
{
    if ((musicpath != NULL) || (playpath  != NULL) || (picturepath != NULL) || (videopath != NULL) || (organizerpath != NULL) || (zencastpath != NULL)) {
        dialogProgress = create_dialogProgress();
        GtkWidget * buttonCloseProgress = lookup_widget (GTK_WIDGET (dialogProgress), "buttonCloseProgress");
        progressbar = GTK_PROGRESS_BAR(lookup_widget (GTK_WIDGET (dialogProgress), "progressbar"));
        gtk_widget_show (dialogProgress);
        gtk_list_store_clear (store);
        int ret = refresh_list ();
        if (ret==0) {
            gtk_progress_bar_set_fraction(progressbar,0.50);
            do_pending();
            g_slist_foreach (updates, (GFunc) update_table, NULL);
            do_pending();
            gtk_progress_bar_set_fraction(progressbar,0.55);
     
            GtkWidget *treeChanges =
            lookup_widget (GTK_WIDGET (windowMain), "treeChanges");
            GtkTreeSelection *selection =
            gtk_tree_view_get_selection (GTK_TREE_VIEW (treeChanges));
            GtkTreeIter iter;
            GtkTreeModel *model;
            model = gtk_tree_view_get_model(GTK_TREE_VIEW (treeChanges));
            gtk_tree_model_get_iter_first(model,&iter);
            gtk_tree_selection_select_iter (selection, &iter);
            do_pending();
     
            force_direction = LOCAL_TO_REMOTE;
            change_table (LOCAL_TO_REMOTE);
            gtk_progress_bar_set_fraction(progressbar,0.60);
            display_message ("Sending files");
            g_slist_foreach (updates, (GFunc) send_items, NULL);
            gtk_progress_bar_set_fraction(progressbar,0.80);
            updates = g_slist_reverse (updates);
            display_message ("Removing files");
            g_slist_foreach (updates, (GFunc) delete_items, NULL);
            gtk_progress_bar_set_fraction(progressbar,1.00);
            display_message ("Done");
            gtk_widget_set_sensitive(buttonCloseProgress,TRUE);
        } else {
            display_message("No device found");
            gtk_widget_set_sensitive(buttonCloseProgress,TRUE);
        }
        if (device != NULL) {
            LIBMTP_Release_Device (device);
        }
    } else {
        on_buttonSetup_clicked(NULL,NULL);
    }
}


void
on_buttonCloseProgress_clicked (GtkButton * button, gpointer user_data)
{
    gtk_widget_destroy(dialogProgress);
}
