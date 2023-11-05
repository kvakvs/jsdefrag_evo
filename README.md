# JKDefrag Evolution

Built on top of last known open-source version of JKDefrag. Still works on Windows 10 and most certainly 11.

* Modernizing source to build on latest Visual Studio 2019 + C++ 2017 standard. Slowly migrating data structures to C++
  modern smart pointers and smart storage.
* Created CMakelists and moved on to CLion.
* Updated project to VS 2019.
* [C++] Latest Visual-Studio 2005/2008 Project Of JKDefrag Source-Code. Fixed (Will Compile 100%)

## Ideas

* Continue modernizing the source.
* Clean but simple UI.

<hr/>

## Original README

JKDefrag is a disk defragmenter and optimizer for Windows 2000/2003/XP/Vista/2008 compatible with x86/x64 platforms
architecture. Completely automatic and very easy to use, fast, low overhead, with several optimization strategies, and
can handle floppies, USB disks, memory sticks, and anything else that looks like a disk to Windows.

Included are a Windows version, a commandline version (for scheduling by the task scheduler or for use from
administrator scripts), a screensaver version, a DLL library (for use from programming languages), versions for Windows
X64, and the complete sources.

Why use this defragger instead of the standard Windows defragger?

- Much faster.
- Totally automatic, extremely easy to use.
- Optimized for daily use.
- Disk optimization, several strategies.
- Directories are moved to the beginning of the disk.
- Reclaims MFT reserved space after disk-full.
- Maintains free spaces for temporary files.
- Can defragment very full harddisks.
- Can defragment very large files.
- Can defragment individual directories and files.
- Can be run automatically with the Windows Scheduler.
- Can be used from the commandline.
- Can be used as a screen saver.
- Can be run from cdrom or memory stick.
- Sources available, can be customized.
- Supports x86/x64 architecture.

JKDefrag is an open source software by Jeroen Kessels,
this is the "3.36" version, since from version 4,
it was changed to "MyDefrag", which is a closed source freeware.

<hr/>

## Jkdefrag Command Line (doc page from 2008)

JkDefrag is completely automatic. The commandline options are not needed for normal use.
JkDefrag is ready to run, just click the  "JkDefrag"  program. Default behavior is to automatically  
process all the mounted, writable, fixed volumes on your computer. You can change this behavior with
the following commandline options:

For example:  `JkDefrag.exe -a 7 -d 2 -q c: d:`

<dl>
  <dt>-a N</dt>
  <dd>The action to perform. N is a number from 1 to 11, default is 3:

* 1 = Analyze, do not defragment and do not optimize.
* 2 = Defragment only, do not optimize.
* 3 = Defragment and fast optimize [recommended].
* 5 = Force together.
* 6 = Move to end_ of disk.
* 7 = Optimize by sorting all files by name (folder + filename).
* 8 = Optimize by sorting all files by size (smallest first).
* 9 = Optimize by sorting all files by last access (newest first).
* 10 = Optimize by sorting all files by last change (oldest first).
* 11 = Optimize by sorting all files by creation time (oldest first).

  </dd>

  <dt>-e "mask"</dt>
  <dd>
  Exclude files and/or directories that match the mask. The items will not be defragmented
  and will not be moved. Use wildcards '*' and '?' in the mask to match a set of files or directories. If the
  mask contains a space then it must be enclosed in double-quotes.

  Some examples:

  `JkDefrag -e *.avi -e *.zip -e *.log`  
  `JkDefrag -e D:\MySQL\Data\*`
  </dd>

  <dt>-u "mask"</dt>
  <dd>Files that match a mask will be moved to the SpaceHogs area. The program has a built-in
  list  for  all  files  bigger  than  50  megabytes,  files  not  accessed  in  the  last  month,  archives,  files  in  the  
  recycle  bin,  service  pack  files,  and  some  others.  Disable  this  list  by  specifying  the  special  mask  
  "DisableDefaults".  Use  wildcards  '*'  and  '?'  in  the  mask  to  match  a  set  of  files  or  directories.  If  the  
  mask contains a space then it must be enclosed in double-quotes.

  Some examples:

  `JkDefrag -u *.avi -u *.zip -u *.log`  
  `JkDefrag -u D:\MySQL\Data\*`
  </dd>

  <dt>-s N</dt>
  <dd>Slow down to N percent (1...100) of normal speed. Default is 100.  </dd>

  <dt>-f N</dt>
  <dd>Set the size of the free spaces to N percent (0...100) of the size of the volume. The free spaces
  are  room  on  disk  for  temporary  files.  There  are  2  free  spaces,  between  the  3  zones  (directories,  
  regular files, SpaceHogs). Default is 1% (per free space).  </dd>

  <dt>-d N</dt>
  <dd>Select a debug level, controlling the messages that will be written to the logfile. The number N is
  a value from 0 to 6, default is 1:

    * 0 = Fatal errors.
    * 1 = Warning messages [default].
    * 2 = General progress messages.
    * 3 = Detailed progress messages.
    * 4 = Detailed file information.
    * 5 = Detailed gap-filling messages.
    * 6 = Detailed gap-finding messages.
  </dd>

  <dt>-l  "filename"</dt>
  <dd>Specify  a  filename  for  the  logfile.  Default  is  "JkDefrag.log"  and  "JkDefragCmd.log".  
  Specify empty string "" (two double-quotes) to disable the logfile.</dd>

  <dt>-h</dt> 
  <dd>[commandline version only] Show a short help text.</dd>

  <dt>-help</dt> 
  <dd>[commandline version only] Show a short help text.</dd> 

  <dt>--help</dt> 
  <dd>[commandline version only] Show a short help text.</dd>

  <dt>/?</dt> 
  <dd>[commandline version only] Show a short help text.</dd>  

  <dt>-q</dt> 
  <dd>[windows version only] Quit the program when it has finished.</dd>   

  <dt>Items...</dt>
  <dd>The  items  to  be  defragmented  and  optimized,  such  as a  file,  directory,  disk,  mount  point,  or  
  volume,  including  removable  media  such  as  floppies,  USB  disks,  memory  sticks,  and  other  volumes  
  that behave like a harddisk. Wildcards '*' and '?' are allow
  ed to select a set of files. If the item contains a space then it must be enclosed in double-quotes. 

  Some examples:

  `JkDefrag C: D:`  
  `JkDefrag f:\*.log D:\MySQL\Data\*`

 </dd>
</dl>

### Fast Optimization

This optimization strategy is designed for every day use. It moves a minimum of data on the harddisk
and finishes very quickly, but will not fill all the gaps on the disk. The strategy scans for gaps on the
disk and fills them with files from above.

### Sorted optimization

All the sorting optimizations WILL CREATE FRAGMENTS. This is by design, it is not a bug. For more
info see the Frequently Asked Questions.
These sorting optimizations are for incidental use once in a while. They take a lot of running time and
data movement because they rewrite all the data on the disk. The strategies vacate a small area on
disk and then fill it up again with the files in the selected order.

**Sort by name:**

very good for fast program starting. The files used by a particular program will be very
close together on disk.

**Sort by size:**

placing all the small files together at the beg
inning of the disk will dramatically reduce
the average file seek time.

**Sort by last access:**

files that have not been accessed in a while are probably unimportant and are
best sorted to the back.

**Sort by last change:**

placing files together that change a lot (for example databases and log files) will
speed up regular operation of the system.

**Sort by creation time:**

The oldest files on the disk are likely to be important system files, for example used when Windows is booting.

Note:
If you want to sort by last access time then first make sure your virus scanner  (and other  
programs that scan all files on disk) do not change that time.

**Force Together**

Intended for partition resizing. All movable files are moved to the beginning of the disk, even if it  
means fragmenting them to fill gaps that cannot be filled otherwise.

**Move to end_ of disk**

Move all the files to the end_ of the disk, making more room at the beginning of the disk. Intended for
big and rarely used files such as log files, backup archives, installation files, and such. 
