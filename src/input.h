/**
 * input.h - A faux input-buffer.
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
 */

#ifndef _input_h_
#define _input_h_ 1

#include <string>
#include <vector>
#include <glibmm/ustring.h>

/**
 * Singleton class to maintain a faux input-buffer.
 */
class CInput
{

public:

    /**
     * Get access to the singleton instance.
     */
    static CInput *Instance();

    /**
     * Get a character from either our faux buffer, or via curses.
     */
    int get_char();
    int get_wchar(gunichar *wch);

    /**
     * Enqueue some input to the input buffer.
     */
    void add( Glib::ustring input );


protected:

    /**
     * Protected functions to allow our singleton implementation.
     */
    CInput();
    CInput(const CInput &);
    CInput & operator=(const CInput &);

private:

    /**
     * The single instance of this class.
     */
    static CInput *pinstance;

    /**
     * Our pending input.
     */
    Glib::ustring m_pending;

    /**
     * The current position within our pending-input string.
     */
    size_t m_offset;

};

#endif /* _history_h_ */
