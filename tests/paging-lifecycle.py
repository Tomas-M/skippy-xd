#!/usr/bin/env python3
"""Run with a Skippy binary and optional xlunch binary; needs Xvfb, Openbox and xdotool."""
import ctypes
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

skippy=str(Path(sys.argv[1]).resolve())
xlunch=str(Path(sys.argv[2]).resolve()) if len(sys.argv)>2 else None
with tempfile.TemporaryDirectory(prefix='skippy-paging-') as directory:
    root=Path(directory)
    read_fd,write_fd=os.pipe()
    server=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1024x768x24','-nolisten','tcp'],pass_fds=(write_fd,),stderr=subprocess.DEVNULL)
    os.close(write_fd)
    assert select.select([read_fd],[],[],5)[0],'Xvfb did not start'
    env=dict(os.environ,DISPLAY=':'+os.read(read_fd,32).decode().strip()+'.0',LANG='C.UTF-8',LC_ALL='C.UTF-8',XDG_CACHE_HOME=str(root/'cache'))
    os.close(read_fd)
    profile=root/'skippy.rc'
    profile.write_text('[system]\nclientList=_NET_CLIENT_LIST\npseudoTrans=false\n[appearance]\nanimationDuration=150\n[panel]\nshow=false\n[desktop]\nshow=false\n')
    launcher_profile=root/'xlunch.ini'
    launcher_profile.write_text('[ui]\nmode=focus\n[header]\nvisible=false\n[sidebar]\nvisible=false\n[heading]\nvisible=false\n[grid]\nvisible=false\n[footer]\nvisible=false\n')
    (root/'empty.dsv').write_text('')
    wm=subprocess.Popen(['openbox'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    log=(root/'daemon.log').open('w')
    daemon=None
    children=[]

    # Wait for real X11 visibility rather than an IPC client's lifetime.
    def visible(name):
        return subprocess.run(['xdotool','search','--onlyvisible','--name',name],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0

    # Deliver physical events on the isolated test display.
    def key(name):
        subprocess.run(['xdotool','key',name],env=env,check=True)
        time.sleep(.1)

    # Drive the same launcher cancellation sequence without launching applications.
    def cancel_launcher():
        if not xlunch: return
        process=subprocess.Popen([xlunch,'--ui-config',str(launcher_profile),'--outputonly','-i',str(root/'empty.dsv')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        children.append(process)
        time.sleep(.3)
        key('Escape')
        process.wait(timeout=5)
        time.sleep(2)

    # Start the requested overview and drain startup events before checking it.
    def overview(mode):
        process=subprocess.Popen([skippy,mode],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        children.append(process)
        time.sleep(.6)
        assert visible('^skippy-xd fullscreen window$'),mode+' closed immediately'
        return process

    # An unmapped helper must not become an application or dismiss the pager.
    def destroy_hidden():
        display=xlib.XOpenDisplay(env['DISPLAY'].encode())
        window=xlib.XCreateSimpleWindow(display,xlib.XDefaultRootWindow(display),0,0,10,10,0,0,0)
        xlib.XSync(display,False); time.sleep(.1)
        xlib.XDestroyWindow(display,window); xlib.XSync(display,False)
        xlib.XCloseDisplay(display)
        time.sleep(.3)

    xlib=ctypes.CDLL('libX11.so.6')
    xlib.XOpenDisplay.argtypes=[ctypes.c_char_p]; xlib.XOpenDisplay.restype=ctypes.c_void_p
    xlib.XDefaultRootWindow.argtypes=[ctypes.c_void_p]; xlib.XDefaultRootWindow.restype=ctypes.c_ulong
    xlib.XCreateSimpleWindow.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_int,ctypes.c_int,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint,ctypes.c_ulong,ctypes.c_ulong]; xlib.XCreateSimpleWindow.restype=ctypes.c_ulong
    xlib.XDestroyWindow.argtypes=[ctypes.c_void_p,ctypes.c_ulong]
    xlib.XSync.argtypes=[ctypes.c_void_p,ctypes.c_int]
    xlib.XCloseDisplay.argtypes=[ctypes.c_void_p]
    try:
        time.sleep(.3)
        daemon=subprocess.Popen([skippy,'--start-daemon','--config',str(profile),'--debug-log'],env=env,stdout=log,stderr=log)
        time.sleep(.3)
        for attempt in range(3):
            cancel_launcher()
            request=overview('--paging')
            destroy_hidden()
            assert visible('^skippy-xd fullscreen window$'),'Hidden window destruction dismissed an empty pager'
            key('Escape'); request.wait(timeout=5)
        print('PASS: repeated empty paging after launcher Escape and hidden-window destruction',flush=True)

        client=subprocess.Popen(['xterm','-T','Skippy lifecycle test','-e','sleep','60'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        children.append(client); time.sleep(.3)
        cancel_launcher()
        request=overview('--paging')
        client.terminate(); client.wait(timeout=5); time.sleep(.3)
        assert visible('^skippy-xd fullscreen window$'),'Closing the last application dismissed the pager'
        key('Escape'); request.wait(timeout=5)
        print('PASS: paging survives the last application closing',flush=True)

        client=subprocess.Popen(['xterm','-T','Skippy lifecycle test','-e','sleep','60'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        children.append(client); time.sleep(.3)
        request=overview('--expose')
        key('Escape'); request.wait(timeout=5)
        assert not visible('^skippy-xd fullscreen window$'),'Expose ignored Escape'
        client.terminate(); client.wait(timeout=5)
        print('PASS: expose cancellation still works',flush=True)
    finally:
        for process in [*children,daemon,wm,server]:
            if process is not None and process.poll() is None:
                process.terminate(); process.wait(timeout=5)
        log.close()
