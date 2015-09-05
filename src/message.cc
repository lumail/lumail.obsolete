/**
 * message.cc - A class for working with a single message.
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

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <pcrecpp.h>


#include "debug.h"
#include "file.h"
#include "global.h"
#include "lua.h"
#include "message.h"
#include "maildir.h"
#include "utfstring.h"


/**
 * Constructor.
 */
CMessage::CMessage(std::string filename)
{
    m_path         = filename;
    m_date         = 0;
    m_time_cache   = 0;
    m_read         = false;
    m_message      = NULL;
    m_fd           = -1;

#ifdef LUMAIL_DEBUG
    std::string dm = "CMessage::CMessage(";
    dm += m_path;
    dm += ");";
    DEBUG_LOG( dm );
#endif
}


/**
 * Destructor.
 */
CMessage::~CMessage()
{
    close_message();


#ifdef LUMAIL_DEBUG
    std::string dm = "CMessage::~CMessage(";
    dm += m_path;
    dm += ");";
    DEBUG_LOG( dm );
#endif

    /**
     * If we've parsed any attachments then free them
     */
    if ( m_attachments.size() > 0 )
    {
        for (CAttachment *cur : m_attachments)
        {
            DEBUG_LOG( "Deleting attachment object: " + cur->name() );
            delete( cur );
        }
    }

}

/**
 * If the message was parsed correctly, m_message should not be NULL.
 */
bool CMessage::is_valid()
{
    return ( m_message != NULL );
}


/**
 * Parse the message.
 *
 * This will use the Lua-defined `mail_filter` if it is defined.
 */
bool CMessage::message_parse()
{
    /**
     * If we've already parsed the message then we're good.
     */
    if ( is_valid() )
        return true;

    /**
     * See if we're filtering the body.
     */
    CGlobal     *global = CGlobal::Instance();
    std::string *filter = global->get_variable("mail_filter");
    std::string *tmp    = global->get_variable("tmp");

    if ( ( filter != NULL ) && ( ! ( filter->empty() ) ) )
    {
        /**
         * Generate a temporary file for the filter output.
         */
        char filename[256] = { '\0' };
        snprintf( filename, sizeof(filename)-1, "%s/body.filter.XXXXXX", tmp->c_str() );

        /**
         * Open the file.
         */
        int fd  = mkstemp(filename);

        /**
         * Build up the command to execute, via cat.
         */
        std::string cmd = "/bin/cat" ;
        assert( CFile::exists( cmd ) );

        cmd += " ";
        cmd += path();
        cmd += "|";
        cmd += *filter;

        /**
         * Run through the popen dance.
         */
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe)
        {
            char buffer[16384] = { '\0' };
            std::string tmp = "";

            while(!feof(pipe))
            {
                if(fgets(buffer, sizeof(buffer)-1, pipe) != NULL)
                    tmp += buffer;

                memset(buffer, '\0', sizeof(buffer));
            }
            pclose(pipe);

            /**
             * Write the body out to disk.
             */
            std::ofstream on;
            on.open(filename, std::ios::binary);
            on.write(tmp.c_str(), tmp.size());
            on.close();

            /**
             * Parse the message, from the temporary file.
             */
            open_message( filename );
        }

        /**
         * Don't leak the filename
         */
        close(fd);
        CFile::delete_file( filename );
        return is_valid();

    }

    /**
     * OK we've not parsed the message, and there is not filter present.
     * so parse the literal message.
     */
    open_message( path().c_str() );

    return is_valid();
}



/**
 * Get the path to the message on-disk.
 */
std::string CMessage::path()
{
    return (m_path);
}

size_t CMessage::size()
{
    struct stat s;

    if (stat(path().c_str(), &s) < 0)
        return -1;

    return s.st_size;
}

/**
 * Update the path to the message.
 */
void CMessage::path( std::string new_path )
{
    m_path = new_path;

    /**
     * Reset the cached stat() data.
     */
    m_time_cache = 0;

    /**
     * Close the message.
     */
    close_message();
}


/**
 * Copy this message to a different maildir.
 */
void CMessage::copy ( const char *destdir )
{
    /* Get the source path */
    std::string source = path();

    /**
     * The new path.
     */
    std::string dest = CMaildir::message_in( destdir, is_new() );

    /**
     * Copy from source to destination.
     */
    CFile::copy( source, dest );
}

/**
 * Remove this message.
 */
void CMessage::remove()
{
    CFile::delete_file( path() );
}

/**
 * Retrieve the current flags for this message.
 */
std::string CMessage::get_flags()
{
    std::string flags = "";
    std::string pth   = path();

    if (pth.empty())
        return (flags);

    size_t offset = pth.find(":2,");
    if (offset != std::string::npos)
        flags = pth.substr(offset + 3);

    /**
     * Sleazy Hack.
     */
    if ( pth.find( "/new/" ) != std::string::npos )
        flags += "N";


    /**
     * Sort the flags, and remove duplicates
     */
    std::sort( flags.begin(), flags.end());
    flags.erase(std::unique(flags.begin(), flags.end()), flags.end());

    return flags;
}

/**
 * Set the flags for this message.
 */
void CMessage::set_flags( std::string new_flags )
{
    /**
     * Sort the flags.
     */
    std::string flags = new_flags;
    std::sort( flags.begin(), flags.end());
    flags.erase(std::unique(flags.begin(), flags.end()), flags.end());

    /**
     * Get the current ending position.
     */
    std::string cur_path = path();
    std::string dst_path = cur_path;

    size_t offset = std::string::npos;

    if ( ( offset =  cur_path.find(":2,") ) != std::string::npos )
    {
        dst_path = cur_path.substr(0, offset);
        dst_path += ":2,";
        dst_path += flags;
    }
    else
    {
        dst_path = cur_path;
        dst_path += ":2,";
        dst_path += flags;
    }


    DEBUG_LOG( "CMessage::set_flags()" + cur_path + " to " + dst_path );
    if ( cur_path != dst_path )
    {
        CFile::move( cur_path, dst_path );
        path( dst_path );
        close_message();
    }
}


/**
 * Add a flag to a message.
 *
 * Return true if the flag was added, false if already present.
 */
bool CMessage::add_flag( char c )
{
    std::string flags = get_flags();

    size_t offset = std::string::npos;

    /**
     * If the flag was missing, add it.
     */
    if ( ( offset = flags.find( c ) ) == std::string::npos )
    {
        flags += c;
        set_flags( flags );
        return true;
    }
    else
        return false;
}


/**
 * Does this message possess the given flag?
 */
bool CMessage::has_flag( char c )
{
    /**
     * Flags are upper-case.
     */
    c = toupper(c);

    if ( get_flags().find( c ) != std::string::npos)
        return true;
    else
        return false;
}

/**
 * Remove a flag from a message.
 *
 * Return true if the flag was removed, false if it wasn't present.
 */
bool CMessage::remove_flag( char c )
{
    c = toupper(c);

    std::string current = get_flags();

    /**
     * If the flag is not present, return.
     */
    if ( current.find( c ) == std::string::npos)
        return false;

    /**
     * Remove the flag.
     */
    std::string::size_type k = 0;
    while((k=current.find(c,k))!=current.npos)
        current.erase(k, 1);

    set_flags( current );

    return true;
}


/**
 * Does this message match the given filter?
 */
bool CMessage::matches_filter( std::string *filter )
{
    assert(filter != NULL);

    if ( strcmp( filter->c_str(), "all" ) == 0 )
        return true;

    if ( strcmp( filter->c_str(), "new" ) == 0 )
    {
        if ( is_new() )
            return true;
        else
            return false;
    }

    /**
     * Is this a header-limit?
     */
    if ( filter->length() > 8 && strncasecmp( filter->c_str(), "HEADER:", 7 ) == 0 )
    {
        /**
         * Find the ":" to split the value.
         */
        size_t offset = filter->find( ":", 8 );
        if ( offset != std::string::npos )
        {
            /**
             * The header we're matching and the pattern against which to limit.
             */
            std::string head    = filter->substr(7,offset-7);
            std::string pattern = filter->substr(offset+1);

            /**
             * Split the header list by "|" and return true if any of
             * them match.
             */
            std::istringstream helper(head);
            std::string tmp;
            while (std::getline(helper, tmp, '|'))
            {
                std::string value = header( tmp );

                if (pcrecpp::RE(pattern, pcrecpp::RE_Options().set_caseless(true)).PartialMatch(value) )
                    return true;
            }
            return false;

        }
    }

    /**
     * OK now we're falling back to matching against the formatted version
     * of the message - as set by `index_format`.
     */
    std::string formatted = format();

    /**
     * Regexp Matching.
     */
    if (pcrecpp::RE(*filter, pcrecpp::RE_Options().set_caseless(true)).PartialMatch(formatted) )
        return true;

    return false;
}


/**
 * Is this message new?
 */
bool CMessage::is_new()
{
    /**
     * A message is new if:
     *
     * It has the flag "N".
     * It does not have the flag "S".
     */
    if ( ( has_flag( 'N' ) ) || ( ! has_flag( 'S' ) ) )
        return true;

    return false;
}


/**
 * Is this message flagged?
 */
bool CMessage::is_flagged()
{
    /**
     * A message is flagged if it has the flag "F".
     */
    if ( has_flag( 'F' ) )
        return true;

    return false;
}


/**
 * Get the message last modified time (cached).
 */
time_t CMessage::mtime()
{
    struct stat s;

    if (m_time_cache != 0)
    {
        return m_time_cache;
    }

    if (stat(path().c_str(), &s) < 0)
        return m_time_cache;

    memcpy(&m_time_cache, &s.st_mtime, sizeof(time_t));
    return m_time_cache;
}


/**
 * Mark the message as read.
 */
bool CMessage::mark_read()
{
    /*
     * Get the current path, and build a new one.
     */
    std::string c_path = path();
    std::string n_path = "";

    size_t offset = std::string::npos;

    /**
     * If we find /new/ in the path then rename to be /cur/
     */
    if ( ( offset = c_path.find( "/new/" ) )!= std::string::npos )
    {
        /**
         * Path component before /new/ + after it.
         */
        std::string before = c_path.substr(0,offset);
        std::string after  = c_path.substr(offset+strlen("/new/"));

        n_path = before + "/cur/" + after;
        if ( rename(  c_path.c_str(), n_path.c_str() )  == 0 )
        {
            path(n_path);
            add_flag( 'S' );
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        /**
         * The file is new, but not in the new folder.
         *
         * That means we need to remove "N" from the flag-component of the path.
         *
         */
        remove_flag( 'N' );
        add_flag( 'S' );
        return true;
    }

}


/**
 * Mark the message as unread.
 */
bool CMessage::mark_unread()
{
    if ( has_flag( 'S' ) )
    {
        remove_flag( 'S' );
        return true;
    }

    return false;
}


/**
 * Mark the message as flagged.
 */
bool CMessage::mark_flagged()
{
    if ( !has_flag( 'F' ) )
    {
        add_flag( 'F' );
        return true;
    }

    return false;
}


/**
 * Mark the message as unflagged.
 */
bool CMessage::mark_unflagged()
{
    if ( has_flag( 'F' ) )
    {
        remove_flag( 'F' );
        return true;
    }

    return false;
}


/**
 * Format the message for display in the header - via the lua format string.
 */
UTFString CMessage::format( std::string fmt )
{
    UTFString result = fmt;

    /**
     * Get the format-string we'll expand from the global
     * setting, if it wasn't supplied.
     */
    if ( result.empty() )
    {
        CGlobal *global  = CGlobal::Instance();
        std::string *fmt = global->get_variable("index_format");
        result = std::string(*fmt);
    }

    /**
     * The variables we know about.
     */
    const char *fields[10] = { "$FLAGS", "$FROM", "$TO", "$SUBJECT",  "$DATE", "$YEAR", "$MONTH", "$MON", "$DAY", 0 };
    const char **std_name = fields;


    /**
     * Iterate over everything we could possibly-expand.
     */
    for( int i = 0 ; std_name[i] ; ++i)
    {

        size_t offset = result.find( std_name[i], 0 );

        if ( ( offset != std::string::npos ) && ( offset < result.size() ) )
        {

            /**
             * The bit before the variable, the bit after, and the body we'll replace it with.
             */
            std::string before = result.substr(0, offset);
            std::string body = "";
            std::string after  = result.substr(offset+strlen(std_name[i]));

            /**
             * Expand the specific variables.
             */
            if ( strcmp(std_name[i] , "$TO" ) == 0 )
            {
                body = header( "To" );
            }
            if ( strcmp(std_name[i] , "$DATE" ) == 0 )
            {
                body = date();
            }
            if ( strcmp(std_name[i] , "$FROM" ) == 0 )
            {
                body += header( "From" );
            }
            if ( strcmp(std_name[i] , "$FLAGS" ) == 0 )
            {
                /**
                 * Ensure the flags are suitably padded.
                 */
                body = get_flags();

                while( body.size() < 4 )
                    body += " ";
            }
            if ( strcmp(std_name[i] , "$SUBJECT" ) == 0 )
            {
                body = header( "Subject" );
            }
            if ( strcmp(std_name[i],  "$YEAR" ) == 0 )
            {
                body = date(EYEAR);
            }
            if ( strcmp(std_name[i],  "$MONTH" ) == 0 )
            {
                body = date(EMONTH);
            }
            if ( strcmp(std_name[i],  "$MON" ) == 0 )
            {
                body = date(EMON);
            }
            if ( strcmp(std_name[i],  "$DAY" ) == 0 )
            {
                body = date(EDAY);
            }

            result = before + body + after;
        }
    }

    /**
     * If the value is still unchanged ..
     */
    if ( result.size() > 1 && result.at(0) == '$' )
    {
        /**
         * See if it is header value we can find.
         */
        result = header( result.substr(1) );
        if ( result.empty() )
            result = "[unset]";
    }

    return( result );
}


/**
 * Retrieve the value of a given header from the message.
 *
 * NOTE: All headers are converted to lower-case prior to lookup.
 *
 */
UTFString CMessage::header( std::string name )
{
    /**
     * If we don't have the set of header:value pairs from the
     * message then open the message for parsing and read them all.
     */
    if ( m_header_values.empty() )
    {
        DEBUG_LOG( "CMessage::header(" + name + ") - Triggering CMessage::headers()" );
        headers();
    }

    /**
     * Lookup the cached values.
     */
    std::string nm(name);
    std::transform(nm.begin(), nm.end(), nm.begin(), tolower);

    /**
     * Headers shouldn't have newlines in them.
     */
    std::string val = m_header_values[nm];
    val.erase(std::remove(val.begin(), val.end(), '\n'), val.end());
    val.erase(std::remove(val.begin(), val.end(), '\r'), val.end());

    return( val );

}



/**
 * Retrieve all headers, and their values, from the message.
 */
std::unordered_map<std::string, UTFString> CMessage::headers()
{
    /**
     * If the headers are empty then read them.
     */
    if ( m_header_values.empty() )
    {
        DEBUG_LOG( "CMessage::headers() - Reading from message:" + path() );

        const char *name;
        const char *value;

        /**
         * Parse the message and return if invalid.
         */
        if ( !message_parse() )
            return m_header_values;

        /**
         * Prepare to iterate.
         */
        GMimeHeaderList *ls   = GMIME_OBJECT (m_message)->headers;
        GMimeHeaderIter *iter = g_mime_header_iter_new ();

        if (g_mime_header_list_get_iter (ls, iter))
        {
            while (g_mime_header_iter_is_valid (iter))
            {
                /**
                 * Get the name + value.
                 */
                name = g_mime_header_iter_get_name (iter);
                value = g_mime_header_iter_get_value (iter);

                /**
                 * Downcase the name.
                 */
                std::string nm(name);
                std::transform(nm.begin(), nm.end(), nm.begin(), tolower);

                /**
                 * Decode the value.
                 */
                char *decoded = g_mime_utils_header_decode_text ( value );

                /**
                 * Store the result.
                 */
                m_header_values[nm] = decoded;

                g_free(decoded);

                if (!g_mime_header_iter_next (iter))
                    break;
            }
        }
        g_mime_header_iter_free (iter);

        /**
         * Close the message.
         */
        close_message();
    }
    else
    {
        DEBUG_LOG( "CMessage::headers() - Cached values maintained: " + path() );
    }

    /**
     * Return the results.
     */
    return( m_header_values );
}


/**
 * Get the date of the message.
 */
std::string CMessage::date(TDate fmt)
{
    /**
     * If we have a date setup, then use it.
     */
    if ( m_date == 0 )
    {
        /**
         * Get the header.
         */
        std::string date = header("Date");

        if ( date.empty() )
        {
            /**
             * The date was empty, so use the mtime.
             */
            m_date = mtime();
        }
        else
        {
            struct tm t;

            /**
             * Find the date-formats we accept.
             *
             * Any from lua.
             */
            CLua *lua = CLua::Instance();
            std::vector<std::string> fmts = lua->table_to_array( "date_formats" );

            /**
             * Add in the ones we know about too.
             */
            fmts.push_back( "%a, %d %b %y %H:%M:%S" );
            fmts.push_back( "%a, %d %b %Y %H:%M:%S" );
            fmts.push_back( "%a, %d %b %y %H:%M:%S %z" );
            fmts.push_back( "%a, %d %b %Y %H:%M:%S %z" );
            fmts.push_back( "%d %b %y %H:%M:%S" );
            fmts.push_back( "%d %b %Y %H:%M:%S" );
            fmts.push_back( "%a %b %d %H:%M:%S GMT %Y" );
            fmts.push_back( "%a %b %d %H:%M:%S MSD %Y" );
            fmts.push_back( "%a %b %d %H:%M:%S BST %Y" );
            fmts.push_back( "%a %b %d %H:%M:%S CEST %Y" );
            fmts.push_back( "%a %b %d %H:%M:%S PST %Y" );
            fmts.push_back( "%a, %d %b %y %H:%M" );
            fmts.push_back( "%a, %d %b %Y %H:%M" );
            fmts.push_back( "%a, %d %b %Y %H.%M.%S" );
            fmts.push_back( "%d-%b-%Y" );
            fmts.push_back( "%m/%d/%y" );
            fmts.push_back( "%d %b %Y" );
            fmts.push_back( "%a %b %d %H:%M:%S %Y" );
            fmts.push_back( "%d.%m.%Y %H:%M:%S" ); /* Date: 30.04.2014 03:41:22 */

            char* rc = NULL;

            char *current_loc = NULL;

            current_loc = setlocale(LC_TIME, NULL);

            if (current_loc != NULL)
            {
                current_loc = strdup(current_loc);
                setlocale(LC_TIME, "C");
            }

            /**
             * For each format.
             */
            for (std::string fmt : fmts)
            {
                if ( rc )
                    break;

                t.tm_sec=0;
                t.tm_min=0;
                t.tm_hour=0;
                rc = strptime(date.c_str(), fmt.c_str(), &t);
            }

            if ( current_loc != NULL )
            {
                setlocale(LC_TIME, current_loc);
                free(current_loc);
            }

            if (!rc)
            {
                /**
                 * Failed to find a date.
                 */
                m_date = -1;

                /**
                 * Prepare an error message.
                 */
                std::string error = "alert(\"Failed to parse date: " + date + "\", 30 );" ;
                CLua *lua = CLua::Instance();
                lua->execute( error );

                /**
                 * Return the unmodified string which is the best we can hope for.
                 */
                return( date );
            }
            char tzsign[2];
            unsigned int tzhours;
            unsigned int tzmins;
            int tzscan = sscanf(rc," %1[+-]%2u%2u",tzsign,&tzhours,&tzmins);
            if (tzscan==3)
            {
                switch(tzsign[0])
                {
                case '+':
                    t.tm_hour -= tzhours;
                    t.tm_min -= tzmins;
                    break;
                case '-':
                    t.tm_hour += tzhours;
                    t.tm_min += tzmins;
                    break;
                }
            }
            else
            {
                /**
                 * Warning, couldn't parse timezone.  Probably "BST" or "EST" or
                 * something like that.  Ignore it.
                 */
            }

            /**
             *  Note: the following line used to use mktime(), until summer time
             * started and everything went off by an hour.  This timezone stuff
             * is unpleasant.
             */
            m_date = timegm(&t);
        }
    }

    if ( fmt == EFULL )
    {
        std::string date = header("Date");
        return( date );
    }

    /**
     * Convert to a time.
     */
    struct tm * ptm;
    ptm = gmtime ( &m_date );


    /**
     * Year number.
     */
    if ( fmt == EYEAR )
    {
        if ( ( m_date != 0 ) && ( m_date != -1 ) )
        {
            char buff[20] = { '\0' };
            snprintf(buff, sizeof(buff)-1, "%d", ( 1900 + ptm->tm_year ) );
            return( std::string(buff) );
        }
        else
            return std::string("$YEAR");
    }

    /**
     * Month name.
     */
    if ( ( fmt == EMONTH ) || ( fmt == EMON ) )
    {
        const char *months[] = { "January",
                                 "February",
                                 "March",
                                 "April",
                                 "May",
                                 "June",
                                 "July",
                                 "August",
                                 "September",
                                 "October",
                                 "November",
                                 "December" };
        if ( ( m_date != 0 ) && ( m_date != -1 ) )
        {
            std::string month = months[ptm->tm_mon];

            if ( fmt == EMON )
                month = month.substr(0,3);

            return( month );
        }
        else
            return std::string("$MONTH");
    }

    /**
     * Day of month.
     */
    if ( fmt == EDAY )
    {
        if ( ( m_date != 0 ) && ( m_date != -1 ) )
        {
            char buff[20] = { '\0' };
            snprintf(buff, sizeof(buff)-1, "%d", ( ptm->tm_mday ) );
            return( std::string(buff) );
        }
        else
            return std::string("$DAY");
    }

    return std::string("$FAIL");
}



/**
 * This is solely used for sorting by message-headers
 */
time_t CMessage::get_date_field()
{
    if ( m_date != 0 )
        return m_date;

    /**
     * Date wasn't cached.  Make it so.
     */
    std::string tmp = date();

    /**
     * Avoid "unused variable" warning.
     */
    (void)(tmp);

    return( m_date );
}


/**
 * Get the body from our message, using GMime.
 */
UTFString CMessage::get_body()
{
    /**
     * The body we'll return back to the caller.  May be empty if there
     * is no text/plain part in the message.
     */
    UTFString result;

    /**
     * Parse the message, if not yet done.
     * Return empty string if parsing failed.
     */
    if ( !message_parse() )
        return result;

    /**
     * Create an iterator to walk over the MIME-parts of the message.
     */
    GMimePartIter *iter =  g_mime_part_iter_new ((GMimeObject *) m_message);
    assert(iter != NULL);

    GMimeObject *last = NULL;

    /**
     * Iterate over the message.
     */
    do
    {
        GMimeObject *part  = g_mime_part_iter_get_current (iter);

        if ( ( GMIME_IS_OBJECT( part ) ) &&
             ( GMIME_IS_PART(part) ) )
        {

            /**
             * Get the content-type
             */
            GMimeContentType *content_type = g_mime_object_get_content_type (part);

            /**
             * LOGGING.
             */
            DEBUG_LOG( "CMessage::get_body() - " + UTFString(content_type->type) + " " + UTFString(content_type->subtype) );

            /**
             * If the content-type is NULL then text/plain is implied.
             *
             * If the content-type is text/plain AND we don't yet have any content
             * then we can try to get it from this part.
             *
             */
            if ( ( ( content_type == NULL ) ||
                   ( g_mime_content_type_is_type (content_type, "text", "plain") ) ) &&
                 ( result.empty() ) )
            {
                result = mime_part_to_text( part );
            }

            /**
             * If we've found text/html save that away for the last-ditch
             * attempt.
             */
            if ( g_mime_content_type_is_type (content_type, "text", "html") )
            {
                last = part ;
            }
        }
    }
    while (g_mime_part_iter_next (iter));


    if ( result.empty() )
    {
        if ( last )
        {
            result = mime_part_to_text( last );
        }
    }

    /**
     * Cleanup.
     */
    g_mime_part_iter_free (iter);


    /**
     * If the result is empty then we'll just revert to reading the
     * message body, and returning that.
     *
     * This can happen if:
     *
     *  * There is no text/plain part of the message.
     *  * The message is bogus.
     *
     */
    if ( result.empty() )
    {
        DEBUG_LOG( "CMessage::get_body() - Fell back to g_mime_message_get_body()" );

        /**
         * This function is depreciated ..
         */
        GMimeObject *x = g_mime_message_get_body( m_message );
        result = mime_part_to_text( x );
    }
    else
    {
        DEBUG_LOG( "CMessage::get_body() - SUCCEEDED With GMime/iconv/etc" );
    }

    /**
     * All done.
     */
    close_message();
    return( result );
}


/**
 * Get the body of the message, as a vector of lines.
 */
std::vector<UTFString> CMessage::body()
{
    std::vector<UTFString> result;

    /**
     * Ensure the message has been read.
     */
    if ( !message_parse() )
        return result;


    /**
     * Attempt to get the body from the message as one
     * long line.
     */
    UTFString body = get_body();

    /**
     * At this point we have a std::string containing the body.
     *
     * If we have a message_filter set then we should pipe this
     * through it.
     *
     */
    CGlobal     *global = CGlobal::Instance();
    std::string *filter = global->get_variable("display_filter");
    std::string *tmp    = global->get_variable("tmp");

    if ( ( filter != NULL ) && ( ! ( filter->empty() ) ) )
    {
        /**
         * Generate a temporary file for the filter output.
         */
        char filename[256] = { '\0' };
        snprintf( filename, sizeof(filename)-1, "%s/msg.filter.XXXXXX", tmp->c_str() );

        /**
         * Open the file.
         */
        int fd  = mkstemp(filename);

        std::ofstream on;
        on.open(filename, std::ios::binary);
        on.write(body.c_str(), body.size());
        on.close();

        /**
         * Build up the command to execute, via cat.
         */
        std::string cmd = "/bin/cat" ;
        assert( CFile::exists( cmd ) );

        cmd += " ";
        cmd += filename;
        cmd += "|";
        cmd += *filter;

        /**
         * Run through the popen dance.
         */
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe)
        {
            char buffer[16384] = { '\0' };
            std::string tmp = "";

            while(!feof(pipe))
            {
                if(fgets(buffer, sizeof(buffer)-1, pipe) != NULL)
                    tmp += buffer;

                memset(buffer, '\0', sizeof(buffer));
            }
            pclose(pipe);

            /**
             * Replace the body we previously generated
             * with that we've read from popen.
             */
            body = tmp;
        }

        /**
         * Don't leak the temporary file.
         */
        close( fd );
        CFile::delete_file( filename );
    }

    /**
     * Split the body into an array, by newlines.
     *
     * TODO: Use "util.h"
     *    std::vector<UTFString> split(const UTFString &s, char delim)
     *
     */
    std::stringstream stream(body);
    std::string line;
    while (std::getline(stream, line))
    {
        result.push_back( line );
    }


    close_message();
    return(result);
}


/**
 * Parse the attachments associated with the current message.
 */
bool CMessage::parse_attachments()
{
    /**
     * If we have attachments already then we're done.
     */
    if ( m_attachments.size() > 0 )
        return false;

    /**
     * Ensure the message has been read.
     */
    if ( !message_parse() )
        return( false );


    /**
     * Get access to our Lua object, so we can lookup the value of
     * the "view_inline_attachments" boolean.
     */
    CLua *lua = CLua::Instance();
    bool view_inline = lua->get_bool( "view_inline_attachments", true );


    int count = 1;

    GMimePartIter *iter =  g_mime_part_iter_new ((GMimeObject *) m_message);
    assert(iter != NULL);

    /**
     * Iterate over the message.
     */
    do
    {
        GMimeObject *part  = g_mime_part_iter_get_current (iter);
        if  (GMIME_IS_MULTIPART( part ) )
            continue;

        /**
         * Name of the attachment, if we found one.
         */
        char *aname = NULL;

        /**
         * Attachment content, if we found one.
         */
        char *adata = NULL;


        /**
         * Get the content-disposition, so that we can determine
         * if we're dealing with an attachment, or an inline-part.
         */
        GMimeContentDisposition *disp = NULL;
        if ( GMIME_IS_OBJECT(part) )
            disp = g_mime_object_get_content_disposition (part);

        if ( ( disp != NULL ) &&
             ( !g_ascii_strcasecmp (disp->disposition, "attachment") ) )
        {
            /**
             * Attempt to get the filename/name.
             */
            aname = (char *)g_mime_object_get_content_disposition_parameter(part, "filename");
            if ( aname == NULL || ( strlen( aname ) < 1 ))
                aname = (char *)g_mime_object_get_content_disposition_parameter(part, "name");
        }


        /**
         * Get the attachment data.
         */
        GMimeStream *mem = g_mime_stream_mem_new();

        if (GMIME_IS_MESSAGE_PART (part))
        {
            GMimeMessage *msg = g_mime_message_part_get_message (GMIME_MESSAGE_PART (part));

            g_mime_object_write_to_stream (GMIME_OBJECT (msg), mem);
        }
        else if ( GMIME_IS_PART(part))
        {
            GMimeDataWrapper *content = g_mime_part_get_content_object (GMIME_PART (part));

            g_mime_data_wrapper_write_to_stream (content, mem);
        }

        /**
         * NOTE: by setting the owner to FALSE, it means unreffing the
         * memory stream won't free the GByteArray data.
         */
        g_mime_stream_mem_set_owner (GMIME_STREAM_MEM (mem), FALSE);

        GByteArray *res =  g_mime_stream_mem_get_byte_array (GMIME_STREAM_MEM (mem));

        /**
         * The actual data from the array, and the size of that data.
         */
        adata = (char *)res->data;
        size_t len = (res->len);

        g_object_unref (mem);

        /**
         * Save the resulting attachment to the array we return.
         */
        if ( adata != NULL )
        {
            char tmp[128] = { '\0' };
            bool is_inline = false;

            if ( aname == NULL || ( strlen( aname ) < 1 ) )
            {
                snprintf(tmp, sizeof(tmp)-1, "inline-part-%d", count );
                count += 1;
                aname = tmp;
                is_inline = true;
            }

            /**
             * We add inline parts only if we've been told to.
             */
            if ( ( view_inline == true ) ||
                 ( view_inline == false && ( is_inline == false ) ) )
            {
                CAttachment *foo = new CAttachment( aname, (void *)adata,(size_t ) len );
                m_attachments.push_back(foo);
            }
        }
    }
    while (g_mime_part_iter_next (iter));

    g_mime_part_iter_free (iter);

    return( true );
}


/**
 * Get the names of attachments to this message.
 */
std::vector<std::string> CMessage::attachments()
{
    std::vector<std::string> paths;

    /**
     * Parse attachments if empty.
     */
    if ( m_attachments.empty() )
        parse_attachments();

    for (CAttachment *cur : m_attachments)
    {
        paths.push_back( cur->name() );
    }

    return( paths );
}


/**
 * Save the given attachment.
 */
bool CMessage::save_attachment( int offset, std::string output_path )
{
    /**
     * Parse attachments if empty.
     */
    if ( m_attachments.empty() )
        parse_attachments();

    /**
     * The UI counts attachments from one-onwards.
     */
    offset -= 1;

    /**
     * Bounds-check.
     */
    if ( offset < 0 || offset >= (int)m_attachments.size() )
        return false;

    /**
     * Get the attachment object.
     */
    CAttachment *cur = m_attachments.at( offset );

    /**
     * Write out the data.
     *
     * TODO: Does this cope with partial writes?  I think it does.
     */
    std::ofstream out(output_path, std::ios::out | std::ios::binary );
    out.write( (char *)cur->body(), cur->size() );
    out.close();

    return( true );
}


/**
 * Return the content of the given attachment.
 */
CAttachment* CMessage::get_attachment( int offset )
{
    /**
     * Parse attachments if empty.
     */
    if ( m_attachments.empty() )
        parse_attachments();

    /**
     * The UI counts attachments from one-onwards.
     */
    offset -= 1;

    /**
     * Bounds-check.
     */
    if ( offset < 0 || offset >= (int)m_attachments.size() )
        return NULL;

    /**
     * Get the attachment object, and return it.
     */
    CAttachment *cur = m_attachments.at( offset );
    return( cur );
}

/**
 * This method is responsible for invoking the Lua on_read_message hook.
 *
 * We record whether this message has previously been displayed to avoid
 * triggering multiple times (i.e. once per screen refresh).
 */
bool CMessage::on_read_message()
{
    /**
     * If we've been invoked, return.
     */
    if ( m_read )
        return false;
    else
        m_read = true;

    /**
     * Call the hook.
     */
    CLua *lua = CLua::Instance();
    lua->execute( "on_read_message(\"" + path() + "\");" );

    /**
     * Hook invoked.
     */
    return true;
}


/**
 * Open & parse the message.
 */
void CMessage::open_message( const char *filename )
{
    DEBUG_LOG( "open_message(" + std::string(filename) + ");" );

    GMimeParser *parser;
    GMimeStream *stream;
    m_fd = open( filename, O_RDONLY, 0);

    if ( m_fd < 0 )
    {
        char *reason = strerror(errno);
        std::string error = "alert(\"Failed to open file ";
        error += filename;
        error += " ";
        error += reason;
        error += "\", 30 );" ;
        CLua *lua = CLua::Instance();
        lua->execute( error );
        return;
    }
    else
    {
        DEBUG_LOG( "file->open : " + std::string( filename ) );
    }

    stream = g_mime_stream_fs_new (m_fd);

    parser = g_mime_parser_new_with_stream (stream);
    g_object_unref (stream);

    m_message = g_mime_parser_construct_message (parser);

    if ( m_message == NULL )
    {
        DEBUG_LOG( "g_mime_parser_construct_message failed in open_message("
                   + std::string(filename) + ")" );
    }

    g_object_unref (parser);
}

/**
 * Close the message.
 */
void CMessage::close_message()
{
    if ( m_message != NULL )
    {
        g_object_unref( m_message );
        m_message = NULL;
    }


    if ( m_fd > 0 )
    {
        DEBUG_LOG( "file->close" );
        close( m_fd );
    }
}



/**
 * Update a basic email, on-disk, to include the named attachments.
 */
void CMessage::add_attachments_to_mail(std::string filename, std::vector<std::string> attachments )
{
    /**
     * When this code is called we have a file, on-disk, which contains something like:
     *
     *    To: foo@bar.com
     *    Subject: moi
     *    From: me@example.com
     *
     *    Body text..
     *    More text..
     *
     *    --
     *    Sig
     *
     *
     * We also have a vector of filenames which should be added as attachments
     * to this mail.
     *
     * If there are no attachments then we could return immediately, which would
     * send a naive/simple/non-MIME message.  Instead we choose to proceed
     * regardless.
     *
     * If we have attachments then we need to add each one in turn, if not we
     * just create a minimal MIME message.  Either way we'll be multi-part/mixed.
     *
     *
     */


    GMimeMessage *message;
    GMimeParser  *parser;
    GMimeStream  *stream;
    int fd;



    if ((fd = open ( filename.c_str(), O_RDONLY, 0)) == -1)
    {
        DEBUG_LOG( "CMessage::add_attachments_to_mail - Failed to open filename for reading" );
        return;
    }

    stream = g_mime_stream_fs_new (fd);

    parser = g_mime_parser_new_with_stream (stream);
    g_object_unref (stream);

    message = g_mime_parser_construct_message (parser);
    g_object_unref (parser);


    GMimeMultipart *multipart;
    GMimePart *attachment;
    GMimeDataWrapper *content;

    /**
     * Create a new multipart message.
     */
    multipart = g_mime_multipart_new();
    GMimeContentType *type;

    /**
     * Handle the mime-type.
     */
    if ( attachments.size() > 0 )
    {
        type = g_mime_content_type_new ("multipart", "mixed");
        g_mime_object_set_content_type (GMIME_OBJECT (multipart), type);
    }


    GMimeContentType *new_type;
    GMimeObject *mime_part;

    mime_part = g_mime_message_get_mime_part (message);
    new_type = g_mime_content_type_new_from_string ("text/plain; charset=UTF-8");
    g_mime_object_set_content_type (mime_part, new_type);

    /**
     * first, add the message's toplevel mime part into the multipart
     */
    g_mime_multipart_add (multipart, g_mime_message_get_mime_part (message));

    /**
     * now set the multipart as the message's top-level mime part
     */
    g_mime_message_set_mime_part (message,(GMimeObject*) multipart);

    for (std::string name : attachments)
    {
        if ((fd = open (name.c_str(), O_RDONLY)) == -1)
        {
            DEBUG_LOG( "CMessage::add_attachments_to_mail - Failed to open attachment" );
            return;
        }

        stream = g_mime_stream_fs_new (fd);

        /**
         * the stream isn't encoded, so just use DEFAULT
         */
        content = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_DEFAULT);

        g_object_unref (stream);

        /**
         * Find the MIME-type of the file.
         */
        CLua *lua = CLua::Instance();
        std::string type = lua->get_mime_type( name );

        /**
         * Here we use the mime-type we've returned and set that for the
         * attachment.
         */
        attachment = g_mime_part_new();
        GMimeContentType *a_type = g_mime_content_type_new_from_string (type.c_str());
        g_mime_part_set_content_object (attachment, content);
        g_mime_object_set_content_type((GMimeObject *)attachment, a_type);
        g_object_unref (content);

        /**
         * set the filename.
         */
        g_mime_part_set_filename (attachment, CFile::basename(name).c_str());

        /**
         * Here we use base64 encoding.
         *
         * NOTE: if you want o get really fancy, you could use
         * g_mime_part_get_best_content_encoding()
         * to calculate the most efficient encoding algorithm to use.
         */
        g_mime_part_set_content_encoding (attachment, GMIME_CONTENT_ENCODING_BASE64);


        /**
         * Add the attachment to the multipart
         */
        g_mime_multipart_add (multipart, (GMimeObject*)attachment);
        g_object_unref (attachment);
    }


    /**
     * now that we've finished referencing the multipart directly (the message still
     * holds it's own ref) we can unref it.
     */
     g_object_unref (multipart);

     /**
      * Output the updated message.  First pick a tmpfile.
      *
      * NOTE: We must use a temporary file.  If we attempt to overwrite the
      * input file we'll get corruption, due to GMime caching.
      */
     CGlobal *global   = CGlobal::Instance();
     std::string *tmp  = global->get_variable( "tmp" );
     char tmpfile[256] = { '\0' };
     snprintf( tmpfile, sizeof(tmpfile)-1, "%s/mytemp.XXXXXX", tmp->c_str() );

     /**
      * Write out the updated message.
      */
     FILE *f = NULL;
     if ((f = fopen ( tmpfile,"wb")) == NULL)
     {
         DEBUG_LOG( "CMessage::add_attachments_to_mail - Failed to open tmpfile" );
         return;
     }
     GMimeStream *ostream = g_mime_stream_file_new (f);
     g_mime_object_write_to_stream ((GMimeObject *) message, ostream);
     g_object_unref(ostream);

     /**
      * Now rename the temporary file over the top of the input
      * message.
      */
     CFile::delete_file( filename );
     CFile::move( tmpfile, filename );
}


/**
 * Return the MIME-types of body-parts.
 */
std::vector<std::string> CMessage::body_mime_parts()
{
    std::vector<std::string> results;

    /**
     * Open the message, if we've not done so.
     */
    if ( !message_parse() )
        return results;


    /**
     * Create an iterator to walk over the MIME-parts of the message.
     */
    GMimePartIter *iter =  g_mime_part_iter_new ((GMimeObject *) m_message);
    assert(iter!=NULL);

    /**
     * Iterate over the message.
     */
    do
    {
        GMimeObject *part  = g_mime_part_iter_get_current (iter);

        if ( ( GMIME_IS_OBJECT( part ) ) && ( GMIME_IS_PART(part) ) )
        {
            /**
             * Get the content-type
             */
            GMimeContentType *content_type = g_mime_object_get_content_type (part);
            gchar *type = g_mime_content_type_to_string ( content_type );

            /**
             * Store it.
             */
            results.push_back( type );

            g_free(type);
        }
    }
    while (g_mime_part_iter_next (iter));

    /**
     * Cleanup.
     */
    g_mime_part_iter_free (iter);
    close_message();

    return( results );
}


/**
 * Return the content of the Nth MIME part.
 */
bool CMessage::get_body_part( int offset, char **data, size_t *length )
{
    /**
     * The return value.
     */
    bool ret = false;

    /**
     * Ensure the message has been read.
     */
    if ( !message_parse() )
        return ret;


    /**
     * Content length
     */
    gint64 len;

    /**
     * Create an iterator
     */
    GMimePartIter *iter =  g_mime_part_iter_new ((GMimeObject *) m_message);
    assert(iter != NULL);

    /**
     * The current object number.
     */
    int count = 1;

    /**
     * Iterate over the message.
     */
    do
    {
        GMimeObject *part  = g_mime_part_iter_get_current (iter);

        if ( ( GMIME_IS_OBJECT( part ) )  && ( GMIME_IS_PART(part) ) )
        {
            if ( count == offset )
            {

                /**
                 * Get the content-type
                 */
                GMimeContentType *content_type = g_mime_object_get_content_type (part);

                /**
                 * If the content-type is NULL then text/plain is implied.
                 */
                if ( ( content_type == NULL ) ||
                     ( g_mime_content_type_is_type (content_type, "text", "plain") ) )
                {

                    /**
                     * We'll use iconv to convert the content to UTF-8 if that is
                     * not already the correct set.
                     */
                    const char *charset;
                    char *converted;

                    /**
                     * Get the content, and setup a memory-stream to read it.
                     */
                    GMimeDataWrapper *c    = g_mime_part_get_content_object( GMIME_PART(part) );
                    GMimeStream *memstream = g_mime_stream_mem_new();

                    /**
                     * Get the size + data.
                     */
                    len       = g_mime_data_wrapper_write_to_stream( c, memstream );
                    guint8 *b = g_mime_stream_mem_get_byte_array((GMimeStreamMem *)memstream)->data;

                    /**
                     * If we have a character set, and it isn't UTF-8 ...
                     */
                    if ( (charset = g_mime_content_type_get_parameter(content_type, "charset")) != NULL &&
                         (strcasecmp(charset, "utf-8") != 0))
                    {

                        /**
                         * Convert it.
                         */
                        iconv_t cv;

                        cv = g_mime_iconv_open ("UTF-8", charset);
                        converted = g_mime_iconv_strndup(cv, (const char *) b, len );
                        if (converted != NULL)
                        {
                            /**
                             * If that succeeded update our return value with it.
                             */
                            size_t conv_len = strlen(converted);
                            *data = (char*)malloc( conv_len + 1 );
                            memcpy( *data, converted, conv_len + 1 );
                            *length = (size_t)conv_len;
                            ret = true;
                            g_free(converted);
                        }
                        else
                        {
                            /**
                             * The conversion failed; but if we have data return
                             * it regardless.
                             */
                            if ( b != NULL )
                            {
                                *data = (char*)malloc( len + 1 );
                                memcpy( *data, b, len );
                                *length = (size_t)len;
                                ret = true;
                            }
                        }
                        g_mime_iconv_close(cv);
                    }
                    else
                    {
                        /**
                         * No character set found, or it is already UTF-8.
                         *
                         * Save the result.
                         */
                        if ( b != NULL )
                        {
                            *data = (char*)malloc( len + 1 );
                            memcpy( *data, b, len );
                            *length = (size_t)len;
                            ret = true;
                        }
                    }
                    g_mime_stream_close(memstream);
                    g_object_unref(memstream);
                }
                else
                {
                    /**
                     * Here the content is not text/plain, return it as is
                     *
                     * Get the content, and setup a memory-stream to read it.
                     */
                    GMimeDataWrapper *c    = g_mime_part_get_content_object( GMIME_PART(part) );
                    GMimeStream *memstream = g_mime_stream_mem_new();

                    /**
                     * Get the size + data.
                     */
                    len       = g_mime_data_wrapper_write_to_stream( c, memstream );
                    guint8 *b = g_mime_stream_mem_get_byte_array((GMimeStreamMem *)memstream)->data;

                    *data = (char*)malloc( len + 1 );
                    memcpy( *data, b, len );
                    *length = (size_t)len;
                    ret = true;

                    g_mime_stream_close(memstream);
                    g_object_unref(memstream);
                }
            }

            count += 1;
        }
    }
    while (g_mime_part_iter_next (iter));

    /**
     * Cleanup.
     */
    g_mime_part_iter_free (iter);
    close_message();

    return( ret );
}


/**
 * Convert the given object to plain-text, decoding as appropriate.
 */
UTFString CMessage::mime_part_to_text( GMimeObject *obj )
{
    /**
     * This shouldn't happen.
     */
    if ( obj == NULL )
    {
        DEBUG_LOG( "NULL message passed to mime_part_to_text()" );
        return "";
    }

    GMimeContentType *content_type = g_mime_object_get_content_type (obj);

    UTFString result;
    const char *charset;
    gint64 len;

    /**
     * Get the content, and create a new stream.
     */
    GMimeDataWrapper *c = g_mime_part_get_content_object( GMIME_PART(obj) );
    GMimeStream *memstream = g_mime_stream_mem_new();

    /**
     * Write the content to the memory-stream.
     */
    len = g_mime_data_wrapper_write_to_stream( c, memstream );
    guint8 *b = g_mime_stream_mem_get_byte_array((GMimeStreamMem *)memstream)->data;

    /**
     * If there is a content-type, and it isn't UTF-8 ...
     */
    if ( (charset =  g_mime_content_type_get_parameter(content_type, "charset")) != NULL &&  (strcasecmp(charset, "utf-8") != 0))
    {
        /**
         * We'll convert it.
         */
        iconv_t cv;

        cv = g_mime_iconv_open ("UTF-8", charset);
        char *converted = g_mime_iconv_strndup(cv, (const char *) b, len );
        if (converted != NULL)
        {
            result = (const char*)converted;
            g_free(converted);
        }
        else
        {
            if ( b != NULL )
                result = std::string((const char *)b, len);
        }
        g_mime_iconv_close(cv);
    }
    else
    {
        /**
         * No content type, or content-type is already correct.
         */
        if ( b != NULL )
            result = std::string((const char *)b, len);
    }
    g_mime_stream_close(memstream);
    g_object_unref(memstream);
    return( result );
}
