/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2018 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * JavaScript Filesystem-style Flash IO functions
 * ----------------------------------------------------------------------------
 */
#include "jswrap_storage.h"
#include "jswrap_flash.h"
#include "jshardware.h"
#include "jsflash.h"
#include "jsvar.h"
#include "jsvariterator.h"
#include "jsparse.h"
#include "jsinteractive.h"
#include "jswrap_json.h"

#ifdef DEBUG
#define DBG(...) jsiConsolePrintf("[Storage] "__VA_ARGS__)
#else
#define DBG(...)
#endif

const int STORAGEFILE_CHUNKSIZE = FLASH_PAGE_SIZE - sizeof(JsfFileHeader); // use 32 for testing

/*JSON{
  "type" : "library",
  "class" : "Storage",
  "ifndef" : "SAVE_ON_FLASH"
}

This module allows you to read and write part of the nonvolatile flash
memory of your device using a filesystem-like API.

Also see the `Flash` library, which provides a low level, more dangerous way
to access all parts of your flash memory.
*/

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "eraseAll",
  "generate" : "jswrap_storage_eraseAll"
}
Erase the flash storage area. This will remove all files
created with `require("Storage").write(...)` as well
as any code saved with `save()` or `E.setBootCode()`.
 */
void jswrap_storage_eraseAll() {
  jsfEraseAll();
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "erase",
  "generate" : "jswrap_storage_erase",
  "params" : [
    ["name","JsVar","The filename - max 8 characters (case sensitive)"]
  ]
}
Erase a single file from the flash storage area.
 */
void jswrap_storage_erase(JsVar *name) {
  jsfEraseFile(jsfNameFromVar(name));
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "read",
  "generate" : "jswrap_storage_read",
  "params" : [
    ["name","JsVar","The filename - max 8 characters (case sensitive)"]
  ],
  "return" : ["JsVar","A string of data"]
}
Read a file from the flash storage area that has
been written with `require("Storage").write(...)`.

This function returns a String that points to the actual
memory area in read-only memory, so it won't use up RAM.

If you evaluate this string with `eval`, any functions
contained in the String will keep their code stored
in flash memory.
*/
JsVar *jswrap_storage_read(JsVar *name) {
  return jsfReadFile(jsfNameFromVar(name));
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "readJSON",
  "generate" : "jswrap_storage_readJSON",
  "params" : [
    ["name","JsVar","The filename - max 8 characters (case sensitive)"]
  ],
  "return" : ["JsVar","An object containing parsed JSON from the file, or undefined"]
}
Read a file from the flash storage area that has
been written with `require("Storage").write(...)`,
and parse JSON in it into a JavaScript object.

This is identical to `JSON.parse(require("Storage").read(...))`
*/
JsVar *jswrap_storage_readJSON(JsVar *name) {
  JsVar *v = jsfReadFile(jsfNameFromVar(name));
  if (!v) return 0;
  JsVar *r = jswrap_json_parse(v);
  jsvUnLock(v);
  return r;
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "readArrayBuffer",
  "generate" : "jswrap_storage_readArrayBuffer",
  "params" : [
    ["name","JsVar","The filename - max 8 characters (case sensitive)"]
  ],
  "return" : ["JsVar","An ArrayBuffer containing data from the file, or undefined"]
}
Read a file from the flash storage area that has
been written with `require("Storage").write(...)`,
and return the raw binary data as an ArrayBuffer.

This can be used:

* In a `DataView` with `new DataView(require("Storage").readArrayBuffer("x"))`
* In a `Uint8Array/Float32Array/etc` with `new Uint8Array(require("Storage").readArrayBuffer("x"))`
*/
JsVar *jswrap_storage_readArrayBuffer(JsVar *name) {
  JsVar *v = jsfReadFile(jsfNameFromVar(name));
  if (!v) return 0;
  JsVar *r = jsvNewArrayBufferFromString(v, 0);
  jsvUnLock(v);
  return r;
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "write",
  "generate" : "jswrap_storage_write",
  "params" : [
    ["name","JsVar","The filename - max 8 characters (case sensitive)"],
    ["data","JsVar","The data to write"],
    ["offset","int","The offset within the file to write"],
    ["size","int","The size of the file (if a file is to be created that is bigger than the data)"]
  ],
  "return" : ["bool","True on success, false on failure"]
}
Write/create a file in the flash storage area. This is
nonvolatile and will not disappear when the device resets
or power is lost.

Simply write `require("Storage").write("MyFile", "Some data")` to write
a new file, and `require("Storage").read("MyFile")` to read it.

If you supply:

* A String, it will be written as-is
* An array, will be written as a byte array (but read back as a String)
* An object, it will automatically be converted to
a JSON string before being written.

You may also create a file and then populate data later **as long as you
don't try and overwrite data that already exists**. For instance:

```
var f = require("Storage");
f.write("a","Hello",0,14);
f.write("a"," ",5);
f.write("a","World!!!",6);
print(f.read("a"));
```

This can be useful if you've got more data to write than you
have RAM available.
*/
bool jswrap_storage_write(JsVar *name, JsVar *data, JsVarInt offset, JsVarInt _size) {
  JsVar *d;
  if (jsvIsObject(data)) {
    d = jswrap_json_stringify(data,0,0);
    offset = 0;
    _size = 0;
  } else
    d = jsvLockAgainSafe(data);
  bool success = jsfWriteFile(jsfNameFromVar(name), d, JSFF_NONE, offset, _size);
  jsvUnLock(d);
  return success;
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "list",
  "generate" : "jswrap_storage_list",
  "return" : ["JsVar","An array of filenames"]
}
List all files in the flash storage area. An array of Strings is returned.

**Note:** This will output system files (eg. saved code) as well as
files that you may have written.
 */
JsVar *jswrap_storage_list() {
  return jsfListFiles();
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "compact",
  "generate" : "jswrap_storage_compact"
}
The Flash Storage system is journaling. To make the most of the limited
write cycles of Flash memory, Espruino marks deleted/replaced files as
garbage and moves on to a fresh part of flash memory. Espruino only
fully erases those files when it is running low on flash, or when
`compact` is called.

`compact` may fail if there isn't enough RAM free on the stack to
use as swap space, however in this case it will not lose data.

**Note:** `compact` rearranges the contents of memory. If code is
referencing that memory (eg. functions that have their code stored in flash)
then they may become garbled when compaction happens. To avoid this,
call `eraseFiles` before uploading data that you intend to reference to
ensure that uploaded files are right at the start of flash and cannot be
compacted further.
 */
void jswrap_storage_compact() {
  jsfCompact();
}

/*JSON{
  "type" : "staticmethod",
  "ifdef" : "DEBUG",
  "class" : "Storage",
  "name" : "debug",
  "generate" : "jswrap_storage_debug"
}
This writes information about all blocks in flash
memory to the console - and is only useful for debugging
flash storage.
 */
void jswrap_storage_debug() {
  jsfDebugFiles();
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "getFree",
  "generate" : "jswrap_storage_getFree",
  "return" : ["int","The amount of free bytes"]
}
Return the amount of free bytes available in
Storage. Due to fragmentation there may be more
bytes available, but this represents the maximum
size of file that can be written.
 */
int jswrap_storage_getFree() {
  return (int)jsfGetFreeSpace(0,true);
}

/*JSON{
  "type" : "staticmethod",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "Storage",
  "name" : "open",
  "generate" : "jswrap_storage_open",
  "params" : [
    ["name","JsVar","The filename - max **7** characters (case sensitive)"],
    ["mode","JsVar","The open mode - must be either `'r'` for read,`'w'` for write , or `'a'` for append"]
  ],
  "return" : ["JsVar","An object containing {read,write,erase}"],
  "return_object" : "StorageFile"
}
Open a file in the Storage area. This can be used for appending data
(normal read/write operations only write the entire file).

**Note:** These files write through immediately - they do not need closing.

*/
JsVar *jswrap_storage_open(JsVar *name, JsVar *modeVar) {
  char mode = 0;
  if (jsvIsStringEqual(modeVar,"r")) mode='r';
  else if (jsvIsStringEqual(modeVar,"w")) mode='w';
  else if (jsvIsStringEqual(modeVar,"a")) mode='a';
  else {
    jsExceptionHere(JSET_ERROR, "Invalid mode %j", modeVar);
    return 0;
  }

  JsVar *f = jspNewObject(0, "StorageFile");
  if (!f) return 0;

  int chunk = 1;

  JsVar *n = jsvNewFromStringVar(name,0,8);
  JsfFileName fname = jsfNameFromVar(n);
  int fnamei = sizeof(fname)-1;
  while (fnamei && fname.c[fnamei-1]==0) fnamei--;
  fname.c[fnamei]=chunk;
  jsvObjectSetChildAndUnLock(f,"name",n);

  int offset = 0; // offset in file
  JsfFileHeader header;
  uint32_t addr = jsfFindFile(fname, &header);
  if (mode=='w') { // write,
    if (addr) { // we had a file - erase it
      jswrap_storagefile_erase(f);
      addr = 0;
    }
  }
  if (mode=='a') { // append
    // Find the last free page
    unsigned char lastCh = 255;
    if (addr) jshFlashRead(&lastCh, addr+jsfGetFileSize(&header)-1, 1);
    while (addr && lastCh!=255 && chunk<255) {
      chunk++;
      fname.c[fnamei]=chunk;
      addr = jsfFindFile(fname, &header);
      if (addr) jshFlashRead(&lastCh, addr+jsfGetFileSize(&header)-1, 1);
    }
    if (addr) {
      // if we have a page, try and find the end of it
      char buf[64];
      bool foundEnd = false;
      while (!foundEnd) {
        int l = STORAGEFILE_CHUNKSIZE-offset;
        if (l<=0) {
          foundEnd = true;
          break;
        }
        if (l>sizeof(buf)) l=sizeof(buf);
        jshFlashRead(buf, addr+offset, l);
        for (int i=0;i<l;i++) {
          if (buf[i]==(char)255) {
            l = i;
            foundEnd = true;
            break;
          }
        }
        offset += l;
      }
    }
    // Now 'chunk' and offset points to the last (or a free) page
  }
  if (mode=='r') {
    // read - do nothing, we're good.
  }

  // TODO: Look through a pre-opened file to find the end
  DBG("Open %j Chunk %d Offset %d addr 0x%08x\n",name,chunk,offset,addr);
  jsvObjectSetChildAndUnLock(f,"chunk",jsvNewFromInteger(chunk));
  jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
  jsvObjectSetChildAndUnLock(f,"addr",jsvNewFromInteger(addr));
  jsvObjectSetChildAndUnLock(f,"mode",jsvNewFromInteger(mode));

  return f;
}

/*JSON{
  "type" : "class",
  "class" : "StorageFile",
  "ifndef" : "SAVE_ON_FLASH"
}

These objects are created from `require("Storage").open`
and allow Storage items to be read/written.

**Note:** `StorageFile` uses the fact that all bits of erased flash memory
are 1 to detect the end of a file. As such you should not write character
code 255 (`"\xFF"`) to these files.
*/

JsVar *jswrap_storagefile_read_internal(JsVar *f, int len) {
  bool isReadLine = len<0;
  char mode = (char)jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"mode",0));
  if (mode!='r') {
    jsExceptionHere(JSET_ERROR, "Can't read in this mode");
    return 0;
  }

  uint32_t addr = (uint32_t)jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"addr",0));
  if (!addr) return 0; // end of file
  int offset = jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"offset",0));
  int chunk = jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"chunk",0));
  JsfFileName fname = jsfNameFromVarAndUnLock(jsvObjectGetChild(f,"name",0));
  int fnamei = sizeof(fname)-1;
  while (fnamei && fname.c[fnamei-1]==0) fnamei--;
  fname.c[fnamei]=chunk;

  JsVar *result = 0;
  char buf[32];
  if (isReadLine) len = sizeof(buf);
  while (len) {
    int remaining = STORAGEFILE_CHUNKSIZE-offset;
    if (remaining<=0) { // next page
      offset = 0;
      if (chunk==255) {
        addr=0;
      } else {
        chunk++;
        fname.c[fnamei]=chunk;
        JsfFileHeader header;
        addr = jsfFindFile(fname, &header);
      }
      jsvObjectSetChildAndUnLock(f,"addr",jsvNewFromInteger(addr));
      jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
      jsvObjectSetChildAndUnLock(f,"chunk",jsvNewFromInteger(chunk));
      remaining = STORAGEFILE_CHUNKSIZE;
      if (!addr) {
        // end of file!
        return result;
      }
    }
    int l = len;
    if (l>sizeof(buf)) l=sizeof(buf);
    if (l>remaining) l=remaining;
    jshFlashRead(buf, addr+offset, l);
    for (int i=0;i<l;i++) {
      if (buf[i]==(char)255) {
        // end of file!
        l = i;
        len = l;
        break;
      }
      if (isReadLine && buf[i]=='\n') {
        l = i+1;
        len = l;
        isReadLine = false; // done
        break;
      }
    }

    if (!l) break;
    if (!result)
      result = jsvNewFromEmptyString();
    if (result)
      jsvAppendStringBuf(result,buf,l);

    len -= l;
    offset += l;
    // if we're still reading lines, set the length to buffer size
    if (isReadLine)
      len = sizeof(buf);
  }
  jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
  return result;
}
/*JSON{
  "type" : "method",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "StorageFile",
  "name" : "read",
  "generate" : "jswrap_storagefile_read",
  "params" : [
    ["len","int","How many bytes to read"]
  ],
  "return" : ["JsVar","A String"],
  "return_object" : "StorageFile"
}
Read data from the file
*/
JsVar *jswrap_storagefile_read(JsVar *f, int len) {
  if (len<0) len=0;
  return jswrap_storagefile_read_internal(f,len);
}
/*JSON{
  "type" : "method",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "StorageFile",
  "name" : "readLine",
  "generate" : "jswrap_storagefile_readLine",
  "return" : ["JsVar","A line of data"],
  "return_object" : "StorageFile"
}
Read a line of data from the file (up to and including `"\n"`)
*/
JsVar *jswrap_storagefile_readLine(JsVar *f) {
  return jswrap_storagefile_read_internal(f,-1);
}




/*JSON{
  "type" : "method",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "StorageFile",
  "name" : "write",
  "generate" : "jswrap_storagefile_write",
  "params" : [
    ["data","JsVar","The data to write"]
  ]
}
Append the given data to a file
*/
void jswrap_storagefile_write(JsVar *f, JsVar *_data) {
  char mode = (char)jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"mode",0));
  if (mode!='w' && mode!='a') {
    jsExceptionHere(JSET_ERROR, "Can't write in this mode");
    return;
  }

  JsVar *data = jsvAsString(_data);
  if (!data) return;
  size_t len = jsvGetStringLength(data);
  if (len==0) return;
  int offset = jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"offset",0));
  int chunk = jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"chunk",0));
  JsfFileName fname = jsfNameFromVarAndUnLock(jsvObjectGetChild(f,"name",0));
  int fnamei = sizeof(fname)-1;
  while (fnamei && fname.c[fnamei-1]==0) fnamei--;
  //DBG("Filename[%d]=%d\n",fnamei,chunk);
  fname.c[fnamei]=chunk;
  uint32_t addr = (uint32_t)jsvGetIntegerAndUnLock(jsvObjectGetChild(f,"addr",0));
  DBG("Write Chunk %d Offset %d addr 0x%08x\n",chunk,offset,addr);
  int remaining = STORAGEFILE_CHUNKSIZE - offset;
  if (!addr) {
    DBG("Write Create Chunk\n");
    if (jsfWriteFile(fname, data, JSFF_NONE, 0, STORAGEFILE_CHUNKSIZE)) {
      JsfFileHeader header;
      addr = jsfFindFile(fname, &header);
      offset = len;
      jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
      jsvObjectSetChildAndUnLock(f,"addr",jsvNewFromInteger(addr));
    } else {
      // there would already have been an exception
    }
    jsvUnLock(data);
    return;
  }
  if (len<remaining) {
    DBG("Write Append Chunk\n");
    // Great, it all fits in
    jswrap_flash_write(data, addr+offset);
    offset += len;
    jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
  } else {
    DBG("Write Append Chunk and create new\n");
    // Fill up this page, do part of old page
    // End of this page
    JsVar *part = jsvNewFromStringVar(data,0,remaining);
    jswrap_flash_write(part, addr+offset);
    jsvUnLock(part);
    // Next page
    if (chunk==255) {
      jsExceptionHere(JSET_ERROR, "File too big!");
      jsvUnLock(data);
      return;
    } else {
      chunk++;
      fname.c[fnamei]=chunk;
      jsvObjectSetChildAndUnLock(f,"chunk",jsvNewFromInteger(chunk));
    }
    // Write Next page
    part = jsvNewFromStringVar(data,remaining,JSVAPPENDSTRINGVAR_MAXLENGTH);
    if (jsfWriteFile(fname, part, JSFF_NONE, 0, STORAGEFILE_CHUNKSIZE)) {
      JsfFileHeader header;
      addr = jsfFindFile(fname, &header);
      offset = len;
      jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
      jsvObjectSetChildAndUnLock(f,"addr",jsvNewFromInteger(addr));
    } else {
      jsvUnLock(data);
      return; // there would already have been an exception
    }
    offset = jsvGetStringLength(part);
    jsvUnLock(part);
    jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(offset));
  }
  jsvUnLock(data);
}

/*JSON{
  "type" : "method",
  "ifndef" : "SAVE_ON_FLASH",
  "class" : "StorageFile",
  "name" : "erase",
  "generate" : "jswrap_storagefile_erase"
}
Erase this file
*/
void jswrap_storagefile_erase(JsVar *f) {
  JsfFileName fname = jsfNameFromVarAndUnLock(jsvObjectGetChild(f,"name",0));
  int fnamei = sizeof(fname)-1;
  while (fnamei && fname.c[fnamei-1]==0) fnamei--;
  // erase all numbered files
  int chunk = 1;
  bool ok = true;
  while (ok) {
    fname.c[fnamei]=chunk;
    ok = jsfEraseFile(fname);
    chunk++;
  }
  // reset everything
  jsvObjectSetChildAndUnLock(f,"chunk",jsvNewFromInteger(1));
  jsvObjectSetChildAndUnLock(f,"offset",jsvNewFromInteger(0));
  jsvObjectSetChildAndUnLock(f,"addr",jsvNewFromInteger(0));
  jsvObjectSetChildAndUnLock(f,"mode",jsvNewFromInteger(0));
}
