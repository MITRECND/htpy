#! /usr/bin/env python

from distutils.core import setup, Extension
from distutils.command.build import build
from distutils.spawn import spawn
import os, os.path

pathjoin = os.path.join

GITVER   = '0.5.31'
PKGNAME  = 'libhtp-' + GITVER
PKGTAR   = PKGNAME + '.tar.gz'
BUILDDIR = 'libhtp-' + GITVER

INCLUDE_DIRS  = ['/usr/local/include', '/opt/local/include', '/usr/include']
LIBRARY_DIRS  = ['/usr/lib', '/usr/local/lib']
EXTRA_OBJECTS = ['-lz']

class htpyMaker(build):
    HTPTAR = PKGTAR
    HTPDIR = BUILDDIR
    include_dirs = [ HTPDIR, pathjoin(HTPDIR, 'htp') ]
    library_dirs = []
    extra_objects  = [ pathjoin(HTPDIR, 'htp/.libs', 'libhtp.a') ]
    libhtp = pathjoin(HTPDIR, 'htp/.libs', 'libhtp.a')
    uname = os.uname()[0]
    if uname != 'Linux':
        EXTRA_OBJECTS.append('-liconv')

    def buildHtp(self):
        # extremely crude package builder
        try:
            os.stat(self.libhtp)
            return None           # assume already built
        except OSError:
            pass

        spawn(['tar', '-zxf', self.HTPTAR], search_path = 1)
        os.chdir(self.HTPDIR)
        spawn([pathjoin('.','autogen.sh')], '-i')
        spawn([pathjoin('.','configure'), 'CFLAGS=-fPIC'])
        spawn(['make'], search_path = 1)
        os.chdir('..')

    def run(self):
        self.buildHtp()
        build.run(self)

INCLUDE_DIRS = htpyMaker.include_dirs + INCLUDE_DIRS
EXTRA_OBJECTS = htpyMaker.extra_objects + EXTRA_OBJECTS

setup (# Distribution meta-data
        name = "htpy",
        version = "0.26",
        description = "python bindings for libhtp",
        author = "Wesley Shields",
        author_email = "wxs@atarininja.org",
        license = "BSD",
        long_description = "Python bindings for libhtp",
        cmdclass = {'build': htpyMaker},
        ext_modules = [Extension("htpy",
                                 sources=["htpy.c"],
                                 include_dirs = INCLUDE_DIRS,
                                 library_dirs = LIBRARY_DIRS,
                                 extra_objects = EXTRA_OBJECTS)],
        url = "http://github.com/MITRECND/htpy")
