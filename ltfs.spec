Summary: IBM Spectrum Archive Single Drive Edition
Name: ltfssde
Version: 2.2.2.0
Release: 9802
Group: Util
Vendor: IBM
License: LGPL
Source: ltfssde_2220_9802.tar.gz
Prereq: /sbin/ldconfig, /usr/bin/awk
Requires:  fuse >= 2.7
Requires:  libxml2
Requires:  libicu
Requires:  net-snmp
BuildRoot: /tmp/rpm/%{name}-%{version}

%define         _prefix  /opt/IBM/ltfs
%define         _sysconf /etc
%define         _ulocal  /usr/local

DocDir:    %{_prefix}/share/doc/%{name}-%{version}

%if %{defined suse_version}
%debug_package
%endif

%description
IBM Spectrum Archive Single Drive Edition
Copyright IBM Corp. 2010, 2016

%prep
%setup -q

%build
rm -rf $RPM_BUILD_ROOT
./configure --prefix=%{_prefix} --libdir=%{_libdir} --sysconfdir=%{_sysconf}
make

%pretrans
if [ -d /usr/local/lib/ltfs ]
then
    mv /usr/local/lib/ltfs /usr/local/lib/ltfs.rpmsave
    ln -s /usr/local/lib/ltfs.rpmsave /usr/local/lib/ltfs
fi

if [ -d /usr/local/lib64/ltfs ]
then
    mv /usr/local/lib64/ltfs /usr/local/lib64/ltfs.rpmsave
    ln -s /usr/local/lib64/ltfs.rpmsave /usr/local/lib64/ltfs
fi

%posttrans
if [ -s /usr/local/lib/ltfs.rpmnew ]
then
    rm -rf /usr/local/lib/ltfs
    mv /usr/local/lib/ltfs.rpmnew /usr/local/lib/ltfs
    /sbin/ldconfig
fi

if [ -s /usr/local/lib64/ltfs.rpmnew ]
then
    rm -rf /usr/local/lib64/ltfs
    mv /usr/local/lib64/ltfs.rpmnew /usr/local/lib64/ltfs
    /sbin/ldconfig
fi

%pre
if cat /proc/mounts | awk {'print $1'} | grep -q "^ltfs$"
then
    echo
    echo "Error: please unmount all LTFS instances before installing this RPM."
    echo
    exit 1
fi

%preun
if cat /proc/mounts | awk {'print $1'} | grep -q "^ltfs$"
then
    echo
    echo "Error: please unmount all LTFS instances before installing this RPM."
    echo
    exit 1
fi
/sbin/service ltfs stop
/sbin/chkconfig --del ltfs

%install
[ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;
make -e prefix=$RPM_BUILD_ROOT%{_prefix} libdir=$RPM_BUILD_ROOT%{_libdir} includedir=$RPM_BUILD_ROOT%{_includedir} sysconfdir=$RPM_BUILD_ROOT%{_sysconf} install
mkdir $RPM_BUILD_ROOT%{_libdir}/ltfs
mv $RPM_BUILD_ROOT%{_libdir}/libdriver-* $RPM_BUILD_ROOT%{_libdir}/ltfs
mv $RPM_BUILD_ROOT%{_libdir}/libiosched-* $RPM_BUILD_ROOT%{_libdir}/ltfs
mv $RPM_BUILD_ROOT%{_libdir}/libkmi-* $RPM_BUILD_ROOT%{_libdir}/ltfs
mkdir -p $RPM_BUILD_ROOT/etc/init.d
cp $RPM_BUILD_ROOT%{_datadir}/ltfs/ltfs $RPM_BUILD_ROOT/etc/init.d
mv $RPM_BUILD_ROOT%{_bindir}/ltfs $RPM_BUILD_ROOT%{_bindir}/ltfs-singledrive
mkdir -p $RPM_BUILD_ROOT%{_docdir}

# Prepare symbolic links (Commands)
[ ! -d $RPM_BUILD_ROOT%{_ulocal}/bin ] && mkdir -p $RPM_BUILD_ROOT%{_ulocal}/bin
ln -s -f %{_bindir}/ltfs-singledrive $RPM_BUILD_ROOT%{_ulocal}/bin/ltfs
ln -s -f %{_bindir}/mkltfs $RPM_BUILD_ROOT%{_ulocal}/bin/mkltfs
ln -s -f %{_bindir}/ltfsck $RPM_BUILD_ROOT%{_ulocal}/bin/ltfsck

# Prepare symbolic links (Libraries)
[ ! -d $RPM_BUILD_ROOT%{_ulocal}/%{_lib} ] && mkdir -p $RPM_BUILD_ROOT%{_ulocal}/%{_lib}
for FN in $RPM_BUILD_ROOT%{_libdir}/*
do
    b=`basename ${FN}`
    [ -f ${FN} ] && ln -s -f %{_libdir}/${b} $RPM_BUILD_ROOT%{_ulocal}/%{_lib}/${b}
done

# Prepare symbolic links (Backends)
ln -s -f  %{_libdir}/ltfs $RPM_BUILD_ROOT%{_ulocal}/%{_lib}/ltfs

# Prepare symbolic links (Doc)
[ ! -d $RPM_BUILD_ROOT%{_ulocal}/share/doc ] && mkdir -p $RPM_BUILD_ROOT%{_ulocal}/share/doc
ln -s -f %{_docdir} $RPM_BUILD_ROOT%{_ulocal}/share/doc/%{name}-%{version}

%post
/sbin/ldconfig
/sbin/chkconfig --add ltfs
/sbin/service ltfs start

if [ -d /usr/local/lib/ltfs.rpmsave ]
then
    mv /usr/local/lib/ltfs /usr/local/lib/ltfs.rpmnew
    mv /usr/local/lib/ltfs.rpmsave /usr/local/lib/ltfs
fi

if [ -d /usr/local/lib64/ltfs.rpmsave ]
then
    mv /usr/local/lib64/ltfs /usr/local/lib64/ltfs.rpmnew
    mv /usr/local/lib64/ltfs.rpmsave /usr/local/lib64/ltfs
fi

%postun
/sbin/ldconfig

%clean
[ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;

%files
%defattr(-,root,root)
%{_bindir}/*
%{_libdir}/*
%{_includedir}/*
%{_datadir}/*
%config /etc/ltfs.conf
%config /etc/ltfs.conf.local
/etc/init.d/ltfs
/usr/local/*
%doc doc/README doc/INSTALL doc/COPYING.LIB doc/NOTICES.txt doc/ltfs.conf.example doc/ltfs.conf.local.example
