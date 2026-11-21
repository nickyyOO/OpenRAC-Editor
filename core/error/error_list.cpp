/**************************************************************************/
/*  error_list.cpp                                                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "error_list.h"

#include "core/string/ustring.h"
#include "core/typedefs.h"

const char *error_names[] = {
	TTRC("OK"), // OK
	TTRC("Something went wrong"), // FAILED
	TTRC("Unavailable"), // ERR_UNAVAILABLE
	TTRC("Not set up"), // ERR_UNCONFIGURED
	TTRC("Not allowed"), // ERR_UNAUTHORIZED
	TTRC("Value out of range"), // ERR_PARAMETER_RANGE_ERROR
	TTRC("Not enough memory"), // ERR_OUT_OF_MEMORY
	TTRC("File not found"), // ERR_FILE_NOT_FOUND
	TTRC("Drive not found"), // ERR_FILE_BAD_DRIVE
	TTRC("Folder path is wrong"), // ERR_FILE_BAD_PATH
	TTRC("No permission to use this file"), // ERR_FILE_NO_PERMISSION
	TTRC("File already in use"), // ERR_FILE_ALREADY_IN_USE
	TTRC("Couldn't open the file"), // ERR_FILE_CANT_OPEN
	TTRC("Couldn't save the file"), // ERR_FILE_CANT_WRITE
	TTRC("Couldn't read the file"), // ERR_FILE_CANT_READ
	TTRC("Unknown file type"), // ERR_FILE_UNRECOGNIZED
	TTRC("File is damaged"), // ERR_FILE_CORRUPT
	TTRC("Other files it needs are missing"), // ERR_FILE_MISSING_DEPENDENCIES
	TTRC("File ended early"), // ERR_FILE_EOF
	TTRC("Couldn't open"), // ERR_CANT_OPEN
	TTRC("Couldn't create"), // ERR_CANT_CREATE
	TTRC("Query failed"), // ERR_QUERY_FAILED
	TTRC("Already in use"), // ERR_ALREADY_IN_USE
	TTRC("Locked"), // ERR_LOCKED
	TTRC("Took too long"), // ERR_TIMEOUT
	TTRC("Couldn't connect"), // ERR_CANT_CONNECT
	TTRC("Couldn't find the address"), // ERR_CANT_RESOLVE
	TTRC("Connection error"), // ERR_CONNECTION_ERROR
	TTRC("Can't acquire resource"), // ERR_CANT_ACQUIRE_RESOURCE
	TTRC("Can't fork"), // ERR_CANT_FORK
	TTRC("Data is damaged"), // ERR_INVALID_DATA
	TTRC("Wrong value"), // ERR_INVALID_PARAMETER
	TTRC("Already exists"), // ERR_ALREADY_EXISTS
	TTRC("Does not exist"), // ERR_DOES_NOT_EXIST
	TTRC("Can't read database"), // ERR_DATABASE_CANT_READ
	TTRC("Can't write database"), // ERR_DATABASE_CANT_WRITE
	TTRC("Compilation failed"), // ERR_COMPILATION_FAILED
	TTRC("Method not found"), // ERR_METHOD_NOT_FOUND
	TTRC("Link failed"), // ERR_LINK_FAILED
	TTRC("Script failed"), // ERR_SCRIPT_FAILED
	TTRC("Cyclic link detected"), // ERR_CYCLIC_LINK
	TTRC("Invalid declaration"), // ERR_INVALID_DECLARATION
	TTRC("Duplicate symbol"), // ERR_DUPLICATE_SYMBOL
	TTRC("Parse error"), // ERR_PARSE_ERROR
	TTRC("Busy"), // ERR_BUSY
	TTRC("Skip"), // ERR_SKIP
	TTRC("Help"), // ERR_HELP
	TTRC("Bug"), // ERR_BUG
	TTRC("Printer on fire"), // ERR_PRINTER_ON_FIRE
};

static_assert(std_size(error_names) == ERR_MAX);
