

/*
 * Electric Fence - Red-Zone memory allocator.
 * Copyright (C) 1987-1999 Bruce Perens <bruce@perens.com>
 * Copyright (C) 2002-2005 Hayati Ayguen <h_ayguen@web.de>, Procitec GmbH
 * License: GNU GPL (GNU General Public License, see COPYING-GPL)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * FILE CONTENTS:
 * internal implementation file
 * contains system/platform dependent paging functions
 */


#ifndef EF_PAGING_H

#define EF_PAGING_H


/*
 * Lots of systems are missing the definition of PROT_NONE.
 */
#ifndef  PROT_NONE
#define  PROT_NONE  0
#endif

/*
 * 386 BSD has MAP_ANON instead of MAP_ANONYMOUS.
 */
#if ( !defined(MAP_ANONYMOUS) && defined(MAP_ANON) )
#define  MAP_ANONYMOUS  MAP_ANON
#endif

/*
 * For some reason, I can't find mprotect() in any of the headers on
 * IRIX or SunOS 4.1.2
 */
/* extern C_LINKAGE int mprotect(void * addr, size_t len, int prot); */

static caddr_t  startAddr = (caddr_t) 0;

#if ( !defined(sgi) && !defined(_AIX) && !defined(_MSC_VER) && !defined(__linux__) )
extern int  sys_nerr;
extern char *  sys_errlist[];
/* extern const char *  const sys_errlist[]; */
#endif


static const char *
stringErrorReport(void)
{
#if defined(WIN32)
  DWORD LastError;
  LPVOID lpMsgBuf;

  LastError = GetLastError();
  FormatMessage( 
                  FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS
                , NULL
                , LastError
                , MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)   /* Default language */
                , (LPTSTR) &lpMsgBuf
                , 0
                , NULL
               );
  return (char*)lpMsgBuf; /* "Unknown error.\n"; */
#elif ( defined(sgi) )
  return strerror(oserror());
#elif ( defined(_AIX) || defined(__linux__) )

  return strerror(errno);
#else
  if ( errno > 0 && errno < sys_nerr )
    return sys_errlist[errno];
  else
    return "Unknown error.\n";
#endif
}


static void
mprotectFailed(void)
{
#if defined(WIN32)
  EF_Exit("\nElectric Fence: VirtualProtect() failed: %s", stringErrorReport());
#else
  EF_Exit("\nElectric Fence: mprotect() failed: %s", stringErrorReport());
#endif
}


/*
 * Create memory.
 * void *Page_Create(size_t size)
 */
static void *
Page_Create(size_t size)
{
  caddr_t    allocation;

#if defined(WIN32)

  allocation = VirtualAlloc(
                              NULL /* startAddr */   /* address of region to reserve or commit */
                            , (DWORD) size            /* size of region */
                            , (DWORD) MEM_COMMIT      /* type of allocation */
                            , (DWORD) PAGE_READWRITE  /* type of access protection */
                            );

  startAddr = (char*)allocation + size;

  if ( (caddr_t)0 == allocation )

    allocation = (caddr_t)0;
    /* EF_Exit("\nElectric Fence: VirtualAlloc() failed: %s", stringErrorReport()); */


#elif defined(MAP_ANONYMOUS)

  /*
   * In this version, "startAddr" is a _hint_, not a demand.
   * When the memory I map here is contiguous with other
   * mappings, the allocator can coalesce the memory from two
   * or more mappings into one large contiguous chunk, and thus
   * might be able to find a fit that would not otherwise have
   * been possible. I could _force_ it to be contiguous by using
   * the MMAP_FIXED flag, but I don't want to stomp on memory mappings
   * generated by other software, etc.
   */
  allocation = (caddr_t) mmap(
                                startAddr
                              , (int)size
                              , PROT_READ|PROT_WRITE
                              , MAP_PRIVATE|MAP_ANONYMOUS
                              , -1
                              , 0
                              );

  #ifndef  __hpux
    /*
     * Set the "address hint" for the next mmap() so that it will abut
     * the mapping we just created.
     *
     * HP/UX 9.01 has a kernel bug that makes mmap() fail sometimes
     * when given a non-zero address hint, so we'll leave the hint set
     * to zero on that system. HP recently told me this is now fixed.
     * Someone please tell me when it is probable to assume that most
     * of those systems that were running 9.01 have been upgraded.
     */
    startAddr = allocation + size;
  #endif

  if ( allocation == (caddr_t)-1 )

    allocation = (caddr_t)0;

    /* EF_Exit("mmap() failed: %s", stringErrorReport()); */

#else
  static int  devZeroFd = -1;

  if ( devZeroFd == -1 ) {
    devZeroFd = open("/dev/zero", O_RDWR);
    if ( devZeroFd < 0 )
      EF_Exit( "open() on /dev/zero failed: %s", stringErrorReport() );
  }

  /*
   * In this version, "startAddr" is a _hint_, not a demand.
   * When the memory I map here is contiguous with other
   * mappings, the allocator can coalesce the memory from two
   * or more mappings into one large contiguous chunk, and thus
   * might be able to find a fit that would not otherwise have
   * been possible. I could _force_ it to be contiguous by using
   * the MMAP_FIXED flag, but I don't want to stomp on memory mappings
   * generated by other software, etc.
   */
  allocation = (caddr_t) mmap(
                                startAddr
                              , (int)size
                              , PROT_READ|PROT_WRITE
                              , MAP_PRIVATE
                              , devZeroFd
                              , 0
                              );

  startAddr = allocation + size;

  if ( allocation == (caddr_t)-1 )
    allocation = (caddr_t)0;

    /* EF_Exit("mmap() failed: %s", stringErrorReport()); */

#endif

  return (void *)allocation;
}




/*
 * allow memory access
 * void  Page_AllowAccess(void * address, size_t size)
 */
void

Page_AllowAccess(void * address, size_t size)

{
#if defined(WIN32)
  DWORD OldProtect, retQuery;
  MEMORY_BASIC_INFORMATION MemInfo;
  size_t tail_size;
  BOOL ret;

  while (size >0)
  {
    retQuery = VirtualQuery(address, &MemInfo, sizeof(MemInfo));
    if (retQuery < sizeof(MemInfo))
      EF_Exit("\nElectric Fence: VirtualQuery() failed\n");
    tail_size = (size > MemInfo.RegionSize) ? MemInfo.RegionSize : size;
    ret = VirtualProtect(
                          (LPVOID) address        /* address of region of committed pages */
                        , (DWORD) tail_size       /* size of the region */
                        , (DWORD) PAGE_READWRITE  /* desired access protection */
                        , (PDWORD) &OldProtect    /* address of variable to get old protection */
                        );
    if (0 == ret)
      mprotectFailed();

    address = ((char *)address) + tail_size;
    size -= tail_size;
  }

#else
  if ( mprotect((caddr_t)address, size, PROT_READ|PROT_WRITE) < 0 )
    mprotectFailed();
#endif
}




/*
 * deny memory access
 * void  Page_DenyAccess(void * address, size_t size)
 */
static void
Page_DenyAccess(void * address, size_t size)
{
#if defined(WIN32)
  DWORD OldProtect, retQuery;
  MEMORY_BASIC_INFORMATION MemInfo;
  size_t tail_size;
  BOOL ret;

  while (size >0)
  {
    retQuery = VirtualQuery(address, &MemInfo, sizeof(MemInfo));
    if (retQuery < sizeof(MemInfo))
      EF_Exit("\nElectric Fence: VirtualQuery() failed\n");
    tail_size = (size > MemInfo.RegionSize) ? MemInfo.RegionSize : size;
    ret = VirtualProtect(
                          (LPVOID) address        /* address of region of committed pages */
                        , (DWORD) tail_size       /* size of the region */
                        , (DWORD) PAGE_NOACCESS   /* desired access protection */
                        , (PDWORD) &OldProtect    /* address of variable to get old protection */
                        );
    if (0 == ret)
      mprotectFailed();

    address = ((char *)address) + tail_size;
    size -= tail_size;
  }

#else
  if ( mprotect((caddr_t)address, size, PROT_NONE) < 0 )
    mprotectFailed();
#endif
}




/*
 * free memory
 * void  Page_Delete(void * address, size_t size)
 */
static void
Page_Delete(void * address, size_t size)
{
#if defined(WIN32)
  DWORD retQuery;
  MEMORY_BASIC_INFORMATION MemInfo;
  size_t tail_size;
  BOOL ret;

  while (size >0)
  {
    retQuery = VirtualQuery(address, &MemInfo, sizeof(MemInfo));
    if (retQuery < sizeof(MemInfo))
      EF_Exit("\nElectric Fence: VirtualQuery() failed\n");
    tail_size = (size > MemInfo.RegionSize) ? MemInfo.RegionSize : size;
    ret = VirtualFree(
                       (LPVOID) address        /* address of region of committed pages */
                     , (DWORD) tail_size       /* size of the region */
                     , (DWORD) MEM_DECOMMIT    /* type of free operation */
                     );
    if (0 == ret)
      Page_DenyAccess(address, tail_size);

    address = ((char *)address) + tail_size;
    size -= tail_size;
  }

#else
  if ( munmap((caddr_t)address, size) < 0 )
    Page_DenyAccess(address, size);
#endif
}


/*
 * function Page_Size() moved to createconf.c
 * instead include file efence_config.h and
 * the EF_PAGE_SIZE define
 */


#endif /* EF_PAGING_H */
