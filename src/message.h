/**
 * message.h - A class for working with a single message.
 *
 * This file is part of lumail: http://lumail.org/
 *
 * Copyright (c) 2013-2014 by Steve Kemp.  All rights reserved.
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
 */

#pragma once

#include <string>
#include <stdint.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <gmime/gmime.h>
#include <unordered_map>

#include "utfstring.h"
#include "attachment.h"


class CMaildir;

/**
 * A class for working with a single message.
 *
 * The constructor will be passed a reference to a filename, which is assumed
 * to be file beneath a Maildir folder.
 *
 */
class CMessage
{

public:

    /**
     * Used for formatting dates.
     */
    enum TDate { EYEAR, EDAY, EMONTH, EMON, EFULL };


    /**
     * Constructor
     */
    CMessage(std::string filename);

    /**
     * Destructor.
     */
    ~CMessage();

    /**
     * Get the path to the message, on-disk.
     */
    std::string path();

    /**
     * Update the path to the message, on-disk.
     */
    void path( std::string new_path );

    /**
     * Return the size of the message on disk.
     */
    size_t size();

    /**
     * Copy this message to a different maildir.
     */
    void copy( const char *destdir );

    /**
     * Remove this message.
     */
    void remove();

    /**
     * Format the message for display in the header - via the Lua format string.
     */
    UTFString format( std::string fmt = "");

    /**
     * Retrieve the current flags for this message.
     */
    std::string get_flags();

    /**
     * Set the flags for this message.
     */
    void set_flags( std::string new_flags );

    /**
     * Add a flag to a message.
     */
    bool add_flag( char c );

    /**
     * Does this message possess the given flag?
     */
    bool has_flag( char c );

    /**
     * Remove a flag from a message.
     */
    bool remove_flag( char c );

    /**
     * Does this message match the given filter?
     */
    bool matches_filter( std::string *filter );

    /**
     * Is this message new?
     */
    bool is_new();

    /**
     * Mark a message as unread.
     */
    bool mark_unread();

    /**
     * Mark a message as read.
     */
    bool mark_read();

    /**
     * Is this message flagged?
     */
    bool is_flagged();

    /**
     * Mark a message as unflagged.
     */
    bool mark_unflagged();

    /**
     * Mark a message as flagged.
     */
    bool mark_flagged();

    /**
     * Get the message last modified time (cached).
     */
    time_t mtime();

    /**
     * Retrieve the value of a given header from the message.
     *
     * NOTE: All headers are converted to lower-case prior to lookup.
     *
     */
    UTFString header( std::string name);

    /**
     * Retrieve all headers, and their values, from the message.
     */
    std::unordered_map<std::string, UTFString> headers();

    /**
     * Get the date of the message.
     */
    std::string date(TDate fmt = EFULL);

    /**
     * Get the body of the message, as a vector of lines.
     */
    std::vector<UTFString> body();

    /**
     * Get the names of attachments to this message.
     */
    std::vector<std::string> attachments();

    /**
     * Save the given attachment.
     */
    bool save_attachment( int offset, std::string output_path );

    /**
     * Get the body of the attachment.
     */
    CAttachment* get_attachment( int offset );

    /**
     * This is solely used for sorting by message-headers
     */
    time_t get_date_field();

    /**
     * Call the on_read_message() hook for this object.
     *
     * NOTE: This will only succeed once.
     */
    bool on_read_message();

    /**
     * Update a basic email, on-disk, to include the named attachments.
     */
    static void add_attachments_to_mail(std::string filename, std::vector<std::string> attachments );

    /**
     * Return the MIME-types of body-parts.
     */
    std::vector<std::string> body_mime_parts();

    /**
     * Return the content of the Nth MIME-part.
     */
    bool get_body_part( int offset, char **data, size_t *len );

private:

    /**
     * The GMime message object.
     */
    GMimeMessage *m_message;

    /**
     * Helper for decoding a body.
     */
    UTFString mime_part_to_text( GMimeObject *obj );

    /**
     * Is the message parsed correctly ?
     */
    bool is_valid();

    /**
     * The file-descriptor for this message.
     */
    int m_fd;

    /**
     * Parse the message with gmime.
     */
    void open_message( const char *filename );

    /**
     * Cleanup the message with gmime.
     */
    void close_message();

    /**
     * Have we invoked the on_read_message hook?
     */
    bool m_read;

    /**
     * Cache of the mtime of the file.
     */
    time_t m_time_cache;

    /**
     * Cached map of header names + values.
     *
     * e.g. Date: foo, Subject: bar, To: xxx, From: foo.
     */
    std::unordered_map<std::string, UTFString> m_header_values;

    /**
     * Parse the message, if that hasn't been done.
     * Returns false if parsing failed.
     *
     * NOTE:  This calls the Lua-defined "msg_filter" if that is set.
     */
    bool message_parse();

    /**
     * Parse attachments.
     */
    bool parse_attachments();

    /**
     * Get the text/plain part of the message, via GMime.
     */
    UTFString get_body();

    /**
     * The file we represent.
     */
    std::string m_path;


    /**
     * Cached time/date object.
     */
    time_t m_date;


    /**
     * Cached attachments belonging to this message.
     */
    std::vector<CAttachment *> m_attachments;

};
