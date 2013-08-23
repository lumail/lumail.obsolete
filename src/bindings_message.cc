/**
 * bindings_message.cc - Bindings for all message-related Lua primitives.
 *
 * This file is part of lumail: http://lumail.org/
 *
 * Copyright (c) 2013 by Steve Kemp.  All rights reserved.
 *
 **
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 dated June, 1991, or (at your
 * option) any later version.
 *
 * On Debian GNU/Linux systems, the complete text of version 2 of the GNU
 * General Public License can be found in `/usr/share/common-licenses/GPL-2'
 *
 */


#include <algorithm>
#include <cursesw.h>
#include <fstream>
#include <iostream>
#include <string.h>


#include "bindings.h"
#include "file.h"
#include "global.h"
#include "lang.h"
#include "lua.h"
#include "maildir.h"
#include "message.h"
#include "utfstring.h"
#include "variables.h"



int unused __attribute__((unused));



/**
 * Get the body of the message, as displayed.
 */
int body(lua_State * L)
{
    /**
     * Get the path (optional) to the message.
     */
    const char *str = lua_tostring(L, -1);

    CMessage *msg = get_message_for_operation( str );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }

    /**
     * Get the body, via the on_get_body() call back.
     *
     * If that fails then get the body via parsing and filtering.
     */
    std::vector<UTFString> body;
    CLua *lua = CLua::Instance();
    body = lua->on_get_body();

    if ( body.empty() )
        body = msg->body();


    if ( body.empty() )
        lua_pushnil(L);
    else
    {
        /**
         * Convert the vector of arrays into a string.
         */
        UTFString res;
        std::vector<UTFString>::iterator it;
        for( it = body.begin(); it != body.end(); ++it )
        {
            res += (*it);
            res += "\n";
        }

        lua_pushstring(L, res.c_str());
    }

    if ( str != NULL )
        delete( msg );

    return( 1 );
}


/**
 * Compose a new mail.
 */
int compose(lua_State * L)
{
    /**
     * Prompt for the recipient
     */
    lua_pushstring(L, "To: " );
    int ret = prompt( L);
    if ( ret != 1 )
    {
        lua_pushstring(L, "Error receiving recipient." );
        return( msg(L ) );
    }

    const char *recipient = lua_tostring(L,-1);
    if ( strlen(recipient) < 1 )
    {
        lua_pushstring(L, "Empty recipient, aborting." );
        return( msg(L ) );
    }

    /**
     * Prompt for subject.
     */
    lua_pushstring(L, "Subject: " );
    ret = prompt( L);
    if ( ret != 1 )
    {
        lua_pushstring(L, "Error receiving subject" );
        return( msg(L ) );
    }

    /**
     * Get the subject.
     */
    const char *subject         = lua_tostring(L,-1);
    const char *default_subject = "No subject";

    /**
     * If empty, use the default.
     */
    if ( strlen(subject) < 1 )
        subject = default_subject;

    CGlobal *global   = CGlobal::Instance();
    std::string *from = global->get_variable( "from" );
    std::string *tmp  = global->get_variable( "tmp" );


    /**
     * Generate a temporary file for the message body.
     */
    char filename[256] = { '\0' };
    snprintf( filename, sizeof(filename)-1, "%s/mytemp.XXXXXX", tmp->c_str() );

    /**
     * Open the temporary file.
     */
    int fd = mkstemp(filename);

    if (fd == -1)
        return luaL_error(L, "Failed to create a temporary file.");

    /**
     * .signature handling.
     */
    std::string sig = "";

    lua_getglobal(L, "get_signature" );
    if (lua_isfunction(L, -1))
    {
        lua_pushstring(L, from->c_str() );
        lua_pushstring(L, recipient );
        lua_pushstring(L, subject );
        if (! lua_pcall(L, 3, 1, 0) )
        {
            sig = lua_tostring(L,-1);
        }
    }

    /**
     * To
     */
    unused=write(fd, "To: ", strlen( "To: "));
    unused=write(fd, recipient, strlen( recipient ));
    unused=write(fd, "\n", 1 );

    /**
     * Subject.
     */
    unused=write(fd, "Subject: ", strlen( "Subject: " ) );
    unused=write(fd, subject, strlen( subject ) );
    unused=write(fd, "\n", 1 );

    /**
     * From
     */
    unused=write(fd, "From: " , strlen( "From: " ) );
    unused=write(fd, from->c_str(), strlen( from->c_str() ) );
    unused=write(fd, "\n", 1 );

    /**
     * Space
     */
    unused=write(fd, "\n", 1 );

    /**
     * Body:  User will fill out.
     */

    /**
     * .sig
     */
    if ( sig.empty() )
    {
        unused=write(fd, "\n-- \n", strlen("\n-- \n" ) );
    }
    else
    {
        unused=write(fd, sig.c_str(), sig.size() );
    }

    close(fd);

    /**
     * Save the current state of the TTY
     */
    refresh();
    def_prog_mode();
    endwin();

    /**
     * Get the editor.
     */
    std::string cmd = get_editor();

    /**
     * Run the editor.
     */
    cmd += " ";
    cmd += filename;
    unused = system(cmd.c_str());

    /**
     * Reset + redraw
     */
    reset_prog_mode();
    refresh();


    /**
     * Call the on_edit_message hook, with the path to the message.
     */
    call_message_hook( "on_edit_message", filename );


    /**
     * Attachments associated with this mail.
     */
    std::vector<std::string> attachments;


    /**
     * Prompt for confirmation.
     */
    bool cont = true;


    while( cont )
    {
        /**
         * Use prompt_chars() to get the input
         */
        lua_pushstring(L,"Send mail: (y)es, (n)o, or (a)dd an attachment?" );
        lua_pushstring(L,"anyANY");

        ret = prompt_chars(L);
        if ( ret != 1 )
        {
            lua_pushstring(L, "Error receiving confirmation." );
            return( msg(L ) );
        }
        const char * response = lua_tostring(L, -1);


        if (  ( response[0] == 'y' ) ||
              ( response[0] == 'Y' ) )
        {
            cont = false;
        }
        if ( ( response[0] == 'n' ) ||
             ( response[0] == 'N' ) )
        {
            /**
             * Call the on_message_aborted hook, with the path to the
             * message.
             */
            call_message_hook( "on_message_aborted", filename );

            cont = false;
            CFile::delete_file( filename );
            return 0;
        }
        if (  ( response[0] == 'a' ) ||
              ( response[0] == 'A' ) )
        {
            /**
             * Add attachment.
             */
            lua_pushstring(L,"Path to attachment?" );
            ret = prompt( L);
            if ( ret != 1 )
            {
                CFile::delete_file( filename );
                lua_pushstring(L, "Error receiving attachment." );
                return( msg(L ) );
            }
            const char * path = lua_tostring(L, -1);
            if ( path != NULL )
            {
                attachments.push_back( path );
            }
        }
    }

    /**
     **
     **
     *
     * At this point we have a filename containing the text of the
     * email with all the appropriate headers.
     *
     * We also have a vector of filenames which need to be attached
     * to the outgoing mail.
     *
     * We want to combine these two things into something that we
     * can send.
     *
     **
     **
     **
     */
    CMessage::add_attachments_to_mail( filename, attachments );


    /**
     * Call the on_send_message hook, with the path to the message.
     */
    call_message_hook( "on_send_message", filename );


    /**
     * Send the mail.
     */
    std::string *sendmail  = global->get_variable("sendmail_path");
    CFile::file_to_pipe( filename, *sendmail );

    /**
     * Get a filename in the sent-mail path.
     */
    std::string *sent_path = global->get_variable("sent_mail");
    if ( sent_path != NULL )
    {
        std::string archive = CMaildir::message_in( *sent_path, false );
        if ( archive.empty() )
        {
            CFile::delete_file( filename );

            lua_pushstring(L, "error finding save path");
            return( msg(L ) );
        }


        /**
         * If we got a filename then copy the mail there.
         */
        CFile::copy( filename, archive );
    }

    CFile::delete_file( filename );
    return 0;
}


/**
 * Count messages in the selected folder(s).
 */
int count_messages(lua_State * L)
{
    CGlobal *global = CGlobal::Instance();
    std::vector<CMessage *> *messages = global->get_messages();
    assert(messages!=NULL);

    lua_pushinteger(L, messages->size() );
    return 1;
}


/**
 * Get the currently highlighted message-path.
 */
int current_message(lua_State * L)
{
    /**
     * Get the currently selected message.
     */
    CMessage *msg = get_message_for_operation( NULL );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }

    /**
     * If that succeeded store the path.
     */
    std::string source = msg->path();
    if ( !source.empty() )
    {
        lua_pushstring(L, source.c_str());
        return(1);
    }
    else
    {
        return 0;
    }
}


/**
 * Count the lines in the current message.
 */
int count_lines(lua_State * L)
{
    /**
     * Get the currently selected message.
     */
    CMessage *msg = get_message_for_operation( NULL );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }

    /**
     * If that succeeded get the body.
     */
    std::vector<UTFString> body = msg->body();
    lua_pushinteger(L, body.size() );
    return 1;
}


/**
 * Delete a message.
 */
int delete_message( lua_State *L )
{
    /**
     * Get the path (optional).
     */
    const char *str = lua_tostring(L, -1);

    CMessage *msg = get_message_for_operation( str );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }

    /**
     * Call the on_delete_message hook - before we remove the file
     * from disk.
     */
    call_message_hook( "on_delete_message", msg->path().c_str() );


    /**
     * Now delete the file.
     */
    CFile::delete_file( msg->path().c_str() );

    /**
     * Free the message.
     */
    if ( str != NULL )
        delete( msg );

    /**
     * Update messages
     */
    CGlobal *global = CGlobal::Instance();
    global->update_messages();
    global->set_message_offset(0);

    /**
     * We're done.
     */
    return 0;
}


/**
 * Get a header from the current/specified message.
 */
int header(lua_State * L)
{
    /**
     * Get the path (optional), and the header (required)
     */
    const char *header = lua_tostring(L, 1);
    const char *path   = lua_tostring(L, 2);
    if ( header == NULL )
        return luaL_error(L, "Missing header" );

    /**
     * Get the message
     */
    CMessage *msg = get_message_for_operation( path );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }

    /**
     * Get the header.
     */
    std::string value = msg->header( header );
    lua_pushstring(L, value.c_str() );


    if ( path != NULL )
        delete( msg );

    return( 1 );
}


/**
 * Is the named/current message new?
 */
int is_new(lua_State * L)
{
    /**
     * Get the path (optional).
     */
    const char *str = lua_tostring(L, -1);
    int ret = 0;

    CMessage *msg = get_message_for_operation( str );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }
    else
    {
        if ( msg->is_new() )
            lua_pushboolean(L,1);
        else
            lua_pushboolean(L,0);

        ret = 1;
    }

    if ( str != NULL )
        delete( msg );

    return( ret );
}


/**
 * Mark the message as read.
 */
int mark_read(lua_State * L)
{
    /**
     * Get the path (optional).
     */
    const char *str = lua_tostring(L, -1);

    CMessage *msg = get_message_for_operation( str );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }
    else
        msg->mark_read();

    if ( str != NULL )
        delete( msg );

    return( 0 );
}


/**
 * Mark the message as new.
 */
int mark_unread(lua_State * L)
{
    /**
     * Get the path (optional).
     */
    const char *str = lua_tostring(L, -1);

    CMessage *msg = get_message_for_operation( str );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }
    else
        msg->mark_unread();

    if ( str != NULL )
        delete( msg );

    return( 0 );
}


/**
 * Reply to an existing mail.
 */
int reply(lua_State * L)
{
    /**
     * Get the message we're replying to.
     */
    CMessage *mssg = get_message_for_operation( NULL );
    if ( mssg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }


    /**
     * Get the subject, and sender, etc.
     */
    std::string subject = mssg->header("Subject");
    std::string to      = mssg->header("From");
    std::string ref     = mssg->header( "Message-ID" );


    CGlobal *global   = CGlobal::Instance();
    std::string *from = global->get_variable( "from" );
    std::string *tmp  = global->get_variable( "tmp" );


    char filename[256] = { '\0' };
    snprintf( filename, sizeof(filename)-1, "%s/lumail.reply.XXXXXX", tmp->c_str() );


    /**
     * .signature handling.
     */
    std::string sig = "";

    lua_getglobal(L, "get_signature" );
    if (lua_isfunction(L, -1))
    {
        lua_pushstring(L, from->c_str() );
        lua_pushstring(L, to.c_str() );
        lua_pushstring(L, subject.c_str() );
        if (! lua_pcall(L, 3, 1, 0) )
        {
            sig = lua_tostring(L,-1);
        }
    }


    /**
     * Open the temporary file.
     */
    int fd = mkstemp(filename);

    if (fd == -1)
    {
        return luaL_error(L, "Failed to create a temporary file.");
    }

    /**
     * To
     */
    unused=write(fd, "To: ", strlen( "To: "));
    unused=write(fd, to.c_str(), strlen( to.c_str() ));
    unused=write(fd, "\n", 1 );

    /**
     * Subject.
     */
    unused=write(fd, "Subject: ", strlen( "Subject: " ) );
    unused=write(fd, subject.c_str(), strlen( subject.c_str() ) );
    unused=write(fd, "\n", 1 );

    /**
     * From
     */
    unused=write(fd, "From: " , strlen( "From: " ) );
    unused=write(fd, from->c_str(), strlen( from->c_str() ) );
    unused=write(fd, "\n", 1 );

    /**
     * If we have a message-id add that to the references.
     */
    if ( !ref.empty() )
    {
        /**
         * Message-ID might look like this:
         *
         * Message-ID: <"str"> ("comment")
         *
         * Remove the comment.
         */
        unsigned int start = 0;
        if ( ( ref.find('(') ) != std::string::npos )
        {
            size_t end = ref.find(')',start);
            if ( end != std::string::npos )
                ref.erase(start,end-start+1);
        }

        /**
         * If still non-empty ..
         */
        if ( !ref.empty() )
        {
            unused=write(fd, "References: " , strlen( "References: " ) );
            unused=write(fd, ref.c_str(), strlen( ref.c_str() ) );
            unused=write(fd, "\n", 1 );
        }
    }

    /**
     * Space
     */
    unused=write(fd, "\n", 1 );

    /**
     * Body
     */
    std::vector<UTFString> body = mssg->body();
    int lines =(int)body.size();
    for( int i = 0; i < lines; i++ )
    {
        unused=write(fd, "> ", 2 );
        unused=write(fd, body[i].c_str(), strlen(body[i].c_str() ));
        unused=write(fd, "\n", 1 );
    }

    /**
     * Write the signature.
     */
    if ( sig.empty() )
    {
        unused=write(fd, "\n-- \n", strlen("\n-- \n" ) );
    }
    else
    {
        unused=write(fd, sig.c_str(), sig.size() );
    }
    close(fd);

    /**
     * Save the current state of the TTY
     */
    refresh();
    def_prog_mode();
    endwin();

    /**
     * Get the editor.
     */
    std::string cmd = get_editor();

    /**
     * Run the editor.
     */
    cmd += " ";
    cmd += filename;
    unused = system(cmd.c_str());

    /**
     * Reset the screen.
     */
    reset_prog_mode();
    refresh();


    /**
     * Call the on_edit_message hook, with the path to the message.
     */
    call_message_hook( "on_edit_message", filename );

    /**
     * Attachments associated with this mail.
     */
    std::vector<std::string> attachments;


    /**
     * Prompt for confirmation.
     */
    bool cont = true;
    int ret;


    while( cont )
    {
        /**
         * Use prompt_chars() to get the input
         */
        lua_pushstring(L,"Send mail: (y)es, (n)o, or (a)dd an attachment?" );
        lua_pushstring(L,"anyANY");

        ret = prompt_chars(L);
        if ( ret != 1 )
        {
            lua_pushstring(L, "Error receiving confirmation." );
            return( msg(L ) );
        }

        const char * response = lua_tostring(L, -1);


        if (  ( response[0] == 'y' ) ||
              ( response[0] == 'Y' ) )
        {
            cont = false;
        }
        if ( ( response[0] == 'n' ) ||
             ( response[0] == 'N' ) )
        {
            /**
             * Call the on_message_aborted hook, with the path to the
             * message.
             */
            call_message_hook( "on_message_aborted", filename );

            cont = false;
            CFile::delete_file( filename );

            return 0;
        }
        if ( ( response[0] == 'a' ) ||
             ( response[0] == 'A' ) )
        {
            /**
             * Add attachment.
             */
            lua_pushstring(L,"Path to attachment?" );
            ret = prompt( L);
            if ( ret != 1 )
            {
                CFile::delete_file( filename );
                lua_pushstring(L, "Error receiving attachment." );
                return( msg(L ) );
            }
            const char * path = lua_tostring(L, -1);
            if ( path != NULL )
            {
                attachments.push_back( path );
            }
        }
    }


    /**
     **
     **
     *
     * At this point we have a filename containing the text of the
     * email with all the appropriate headers.
     *
     * We also have a vector of filenames which need to be attached
     * to the outgoing mail.
     *
     * We want to combine these two things into something that we
     * can send.
     *
     **
     **
     **
     */
    CMessage::add_attachments_to_mail( filename, attachments );


    /**
     * Call the on_send_message hook, with the path to the message.
     */
    call_message_hook( "on_send_message", filename );


    /**
     * Send the mail.
     */
    std::string *sendmail  = global->get_variable("sendmail_path");
    CFile::file_to_pipe( filename, *sendmail );

    /**
     * Get a filename in the sent-mail path.
     */
    std::string *sent_path = global->get_variable("sent_mail");
    if ( sent_path != NULL )
    {
        std::string archive = CMaildir::message_in( *sent_path, false );
        if ( archive.empty() )
        {
            CFile::delete_file( filename );

            lua_pushstring(L, "error finding save path");
            return( msg(L ) );
        }


        /**
         * If we got a filename then copy the mail there.
         */
        CFile::copy( filename, archive );
    }


    CFile::delete_file( filename );

    /**
     * Now we're all cleaned up mark the original message
     * as being replied to.
     */
    mssg->add_flag( 'R' );


    /**
     * Reset + redraw
     */
    return( 0 );
}


/**
 * Save the current message to a new location.
 */
int save_message( lua_State *L )
{
    const char *str = lua_tostring(L, -1);

    if (str == NULL)
        return luaL_error(L, "Missing argument to save(..)");

    if ( !CFile::is_directory( str ) )
        return luaL_error(L, "The specified destination is not a Maildir" );

    /**
     * Get the message
     */
    CMessage *msg = get_message_for_operation( NULL );
    if ( msg == NULL )
    {
        CLua *lua = CLua::Instance();
        lua->execute( "msg(\"" MISSING_MESSAGE "\");" );
        return( 0 );
    }

    /**
     * Got a message ?
     */
    std::string source = msg->path();

    /**
     * The new path.
     */
    std::string dest = CMaildir::message_in( str, ( msg->is_new() ) );

    /**
     * Copy from source to destination.
     */
    CFile::copy( source, dest );

    /**
     * Remove source.
     */
    CFile::delete_file( source.c_str() );

    /**
     * Update messages
     */
    CGlobal *global = CGlobal::Instance();
    global->update_messages();
    global->set_message_offset(0);

    /**
     * We're done.
     */
    return 0;
}


/**
 * Scroll the message down.
 */
int scroll_message_down(lua_State *L)
{
    int step = lua_tonumber(L, -1);

    CGlobal *global = CGlobal::Instance();
    int cur = global->get_message_offset();
    cur += step;

    global->set_message_offset(cur);
    return (0);
}


/**
 * Scroll the message to the given offset.
 */
int scroll_message_to(lua_State *L)
{
    int offset = lua_tonumber(L, -1);
    if ( offset < 0 )
        offset = 0;

    CGlobal *global = CGlobal::Instance();
    global->set_message_offset(offset);
    return (0);
}

/**
 * Scroll the message up.
 */
int scroll_message_up(lua_State *L)
{
    int step = lua_tonumber(L, -1);

    CGlobal *global = CGlobal::Instance();
    int cur = global->get_message_offset();
    cur -= step;

    if ( cur < 0 )
        cur = 0;

    global->set_message_offset(cur);
    return (0);
}


/**
 * Send an email via lua-script.
 */
int send_email(lua_State *L)
{
    /**
     * Get our temporary directory.
     */
    CGlobal *global  = CGlobal::Instance();
    std::string *tmp = global->get_variable( "tmp" );

    /**
     * Get the values.
     */
    lua_pushstring(L, "to" );
    lua_gettable(L,-2);
    const char *to = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (to == NULL)
        return luaL_error(L, "Missing recipient.");

    lua_pushstring(L, "from" );
    lua_gettable(L,-2);
    const char *from = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (from == NULL)
        return luaL_error(L, "Missing sender.");

    lua_pushstring(L, "subject" );
    lua_gettable(L,-2);
    const char *subject = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (subject == NULL)
        return luaL_error(L, "Missing subject.");

    lua_pushstring(L, "body" );
    lua_gettable(L,-2);
    const char *body = lua_tostring(L, -1);
    lua_pop(L, 1);
    if (body == NULL)
        return luaL_error(L, "Missing body.");


    /**
     * Optional attachments.
     */
    lua_pushstring(L,"attachments" );
    lua_gettable(L,-2);
    std::vector<std::string> filenames;
    if ( lua_istable(L, -1 ) )
    {
        lua_pushnil(L);

        while (lua_next(L, -2))
        {
            const char *path  = lua_tostring(L, -1);
            filenames.push_back( path );
            lua_pop( L , 1);
        }

        lua_pop(L, 1);
    }
    else
        lua_pop(L, 1);


    /**
     * Generate a temporary filename.
     */
    char filename[256] = { '\0' };
    snprintf( filename, sizeof(filename)-1, "%s/send.mail.XXXXXX", tmp->c_str() );

    /**
     * Open the temporary file.
     */
    int fd = mkstemp(filename);

    if (fd == -1)
        return luaL_error(L, "Failed to create a temporary file.");

    /**
     * .signature handling.
     */
    std::string sig = "";

    lua_getglobal(L, "get_signature" );
    if (lua_isfunction(L, -1))
    {
        lua_pushstring(L, from );
        lua_pushstring(L, to );
        lua_pushstring(L, subject );
        if (! lua_pcall(L, 3, 1, 0) )
        {
            sig = lua_tostring(L,-1);
        }
    }

    /**
     * To
     */
    unused=write(fd, "To: ", strlen( "To: "));
    unused=write(fd, to, strlen( to ));
    unused=write(fd, "\n", 1 );

    /**
     * Subject.
     */
    unused=write(fd, "Subject: ", strlen( "Subject: " ) );
    unused=write(fd, subject, strlen( subject ) );
    unused=write(fd, "\n", 1 );

    /**
     * From
     */
    unused=write(fd, "From: " , strlen( "From: " ) );
    unused=write(fd, from, strlen( from ) );
    unused=write(fd, "\n", 1 );

    /**
     * Space
     */
    unused=write(fd, "\n", 1 );

    /**
     * Body.
     */
    unused=write(fd, body, strlen( body ) );

    /**
     * .sig
     */
    if ( sig.empty() )
    {
        unused=write(fd, "\n\n-- \n", strlen("\n\n-- \n" ) );
    }
    else
    {
        unused=write(fd, "\n", 1 );
        unused=write(fd, "\n", 1 );
        unused=write(fd, sig.c_str(), sig.size() );
    }

    close(fd);


    /**
     * Call the on_send_message hook, with the path to the message.
     */
    call_message_hook( "on_send_message", filename );


    /**
     * OK now we're going to send the mail.  Get some settings.
     */
    std::string *sendmail  = global->get_variable("sendmail_path");

    /**
     **
     **
     *
     * At this point we have a filename containing the text of the
     * email with all the appropriate headers.
     *
     * We also have a vector of filenames which need to be attached
     * to the outgoing mail.
     *
     * We want to combine these two things into something that we
     * can send.
     *
     **
     **
     **
     */
    CMessage::add_attachments_to_mail( filename, filenames );



    /**
     * Send the mail.
     */
    CFile::file_to_pipe( filename, *sendmail );

    /**
     * Get a filename in the sentmail path.
     */
    std::string *sent_path = global->get_variable("sent_mail");
    if ( sent_path != NULL )
    {
        /**
         * If we got a filename then copy the mail there.
         */
        std::string archive = CMaildir::message_in( *sent_path, true );
        if ( archive.empty() )
        {
            CFile::delete_file( filename );
            lua_pushstring(L, "error finding save path");
            return( msg(L ) );
        }

        CFile::copy( filename, archive );
    }


    CFile::delete_file( filename );

    return 0;
}


/**
 * Offset within the message we're displaying.
 */
int message_offset(lua_State * L)
{
    /**
     * How many lines we've scrolled down the message.
     */
    CGlobal *global = CGlobal::Instance();
    int offset = global->get_message_offset();
    assert(offset >= 0);

    lua_pushinteger(L, offset);
    return (1);
}

/**
 * Add the given message to the selected set.
 */
int add_selected_message(lua_State * L)
{
    /**
     * get the optional argument.
     */
    const char *str = lua_tostring(L, -1);

    CGlobal *global = CGlobal::Instance();
    CLua    *lua    = CLua::Instance();

    /**
     * The path that is being added.
     */
    std::string path;

    /**
     * default to the current message.
     */
    if (str == NULL)
    {
        int selected = global->get_selected_message();
        std::vector<CMessage*>* display = global->get_messages();

        if ( display->size()  == 0 )
            return 0;

        CMessage *x = display->at(selected);
        path = x->path();
        global->add_message(path.c_str());
    }
    else
    {
        path = std::string(str);
        global->add_message(path);
    }

    global->set_selected_message(0);
    global->update_messages();
    global->set_message_offset(0);

    if ( ! path.empty() )
        lua->execute("on_message_selection(\"" + path + "\");");

    return (0);
}


/**
 * Clear all currently selected messages.
 */
int clear_selected_messages(lua_State * L)
{
    CGlobal *global = CGlobal::Instance();
    global->unset_messages();
    global->set_selected_message(0);
    global->update_messages();
    global->set_message_offset(0);


    /**
     * Call our update with an empty path.
     */
    CLua *lua = CLua::Instance();
    lua->execute("on_message_selection(\"\");");

    return 0;
}


/**
 * Get the currently selected messages.
 */
int selected_messages(lua_State * L)
{
    CGlobal *global = CGlobal::Instance();
    std::vector<std::string> selected = global->get_selected_messages();
    std::vector<std::string>::iterator it;

    /**
     * Create the table.
     */
    lua_newtable(L);

    int i = 1;
    for (it = selected.begin(); it != selected.end(); ++it)
    {
        lua_pushnumber(L,i);
        lua_pushstring(L,(*it).c_str());
        lua_settable(L,-3);
        i++;
    }

    return 1;
}


/**
 * Remove all currently selected messages.  Add single new one.
 */
int set_selected_message(lua_State * L)
{
    /**
     * get the optional argument.
     */
    const char *str = lua_tostring(L, -1);

    CGlobal *global = CGlobal::Instance();
    global->unset_messages();

    /**
     * The path we're adding.
     */
    std::string path;

    /**
     * default to the current message.
     */
    if (str == NULL)
    {
        std::vector<CMessage *> *display = global->get_messages();
        int selected = global->get_selected_message();

        CMessage *x = display->at(selected);
        path = x->path();
        global->add_message(path.c_str());
    }
    else
    {
        path = std::string(str);
        global->add_message(path.c_str());
    }

    global->update_messages();
    global->set_message_offset(0);

    if ( ! path.empty() )
    {
        CLua *lua = CLua::Instance();
        lua->execute("on_message_selection(\"" + path + "\");");
    }

    return (0);
}


/**
 * Toggle the selection state of the currently selected message.
 */
int toggle_selected_message(lua_State * L)
{
    /**
     * get the optional argument.
     */
    const char *str = lua_tostring(L, -1);
    CGlobal *global = CGlobal::Instance();
    std::vector < std::string > smessages = global->get_selected_messages();

    /**
     * default to the current message.
     */
    std::string toggle;

    if (str == NULL)
    {
        std::vector<CMessage *>* display = global->get_messages();
        if ( display->size()  == 0 )
            return 0;

        int selected = global->get_selected_message();
        CMessage *x = display->at(selected);
        toggle = x->path();
    }
    else
    {
        toggle = std::string(str);
    }

    if (std::find(smessages.begin(), smessages.end(), toggle) != smessages.end())
        global->remove_message(toggle);
    else
        global->add_message(toggle);

    global->update_messages();
    global->set_message_offset(0);

    if ( ! toggle.empty() )
    {
        CLua *lua = CLua::Instance();
        lua->execute("on_message_selection(\"" + toggle + "\");");
    }
    return (0);
}
