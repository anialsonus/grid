# %define __jar_repack %{nil}
%define user ignite-web-agent

#-------------------------------------------------------------------------------
#
# Packages' descriptions
#

Name:             ignite-web-agent-2.4.0-SNAPSHOT
Version:          2.4.0
Release:          %{build_id}%{?dist}
Summary:          Apache Ignite Web Agent
Group:            Development/System
License:          ASL 2.0
URL:              https://arenadata.tech/
Source:           %{name}.zip
Requires:         java-1.8.0, chkconfig
Requires(pre):    shadow-utils
Provides:         %{name}
AutoReq:          no
AutoProv:         no
BuildArch:        noarch
%description
Apache Ignite Web Agent is a standalone Java application that allows to establish a connection
between an Arenadata Grid Cluster and Grid Web Console.
Ignite Agent communicates with cluster nodes via a REST interface and connects to
the Grid Web Console via web-socket


#-------------------------------------------------------------------------------
#
# Prepare step: unpack sources
#

%prep
%setup -q -c -n %{name}

#-------------------------------------------------------------------------------
#
# Preinstall scripts
# $1 can be:
#     1 - Initial install
#     2 - Upgrade
#


#-------------------------------------------------------------------------------
#
# Postinstall scripts
# $1 can be:
#     1 - Initial installation
#     2 - Upgrade
#

%post
case $1 in
    1)
        # Add user for service operation
        useradd -r -d %{_datadir}/%{name} -s /usr/sbin/nologin %{user}
        # Change ownership for work and log directories
        chown -vR %{user}:%{user} %{_sharedstatedir}/%{name} %{_var}/log/%{name}
        # Install alternatives
        # Commented out until ignitevisorcmd / ignitesqlline is ready to work from any user
        #update-alternatives --install %{_bindir}/ignitevisorcmd ignitevisorcmd %{_datadir}/%{name}/bin/ignitevisorcmd.sh 0
        #update-alternatives --auto ignitevisorcmd
        #update-alternatives --display ignitevisorcmd
        #update-alternatives --install %{_bindir}/ignitesqlline ignitesqlline %{_datadir}/%{name}/bin/sqlline.sh 0
        #update-alternatives --auto ignitesqlline
        #update-alternatives --display ignitesqlline
        ;;
    2)
        :
        ;;
esac


#-------------------------------------------------------------------------------
#
# Pre-uninstall scripts
# $1 can be:
#     0 - Uninstallation
#     1 - Upgrade
#

%preun
case $1 in
    0)
        :
        ;;
    1)
        :
        ;;
esac


#-------------------------------------------------------------------------------
#
# Post-uninstall scripts
# $1 can be:
#     0 - Uninstallation
#     1 - Upgrade
#

%postun
case $1 in
    0)
        # Remove user
        userdel %{user}
        # Remove service PID directory
        rm -rfv /var/run/%{name}
        ;;
    1)
        :
        ;;
esac


#-------------------------------------------------------------------------------
#
# Prepare packages' layout
#

%install

ls -la

cd $(ls)

ls -la

# Create base directory structure
mkdir -p %{buildroot}%{_datadir}/%{name} \
         %{buildroot}%{_sysconfdir}/%{name} \
         %{buildroot}%{_sysconfdir}/systemd/system \
         %{buildroot}%{_sysconfdir}/profile.d


# Copy nessessary files and remove *.bat files

cp -rf * %{buildroot}%{_datadir}/%{name}
find %{buildroot}%{_datadir}/%{name}/ -name *.bat -exec rm -rf {} \;

# Setup configuration
cp -rf %{_sourcedir}/web-agent/default.properties %{buildroot}%{_sysconfdir}/%{name}
ln -sf %{_sysconfdir}/%{name} %{buildroot}%{_datadir}/%{name}/default.properties

cp %{_sourcedir}/web-agent/ignite-web-agent.sh %{buildroot}/%{_sysconfdir}/profile.d/ignite-web-agent.sh

# Setup systemctl service
cp -rf %{_sourcedir}/web-agent/ignite-web-agent.service %{buildroot}%{_sysconfdir}/systemd/system/%{user}.service
for file in %{buildroot}%{_sysconfdir}/systemd/system/%{user}.service
do
    sed -i -r -e "s|#name#|%{user}|g" \
              -e "s|#user#|%{user}|g" \
        ${file}
done

for script in $(ls %{buildroot}%{_datadir}/%{name}/*.sh | xargs -n 1 basename);
do
    ln -s %{_datadir}/%{name}/${script} %{buildroot}%{_datadir}/%{name}/$(echo ${script} | sed s/.sh$//g)
done

#-------------------------------------------------------------------------------
#
# Package file list check
#
%files

%attr(0644,root,root)       %{_sysconfdir}/profile.d/ignite-web-agent.sh

%dir %{_sysconfdir}/%{name}

%{_datadir}/%{name}/
%{_sysconfdir}/systemd/system/%{user}.service


%config(noreplace) %{_sysconfdir}/%{name}/


#-------------------------------------------------------------------------------
#
# Changelog
#
%changelog
