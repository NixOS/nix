#!/usr/bin/env bash

# 
# setup
# 
	set -eu
	set -o pipefail

	readonly NIX_LEGACY_DAEMON1_BASE="org.nixos.activate-system"

	readonly NIX_DAEMON_SERVICE_BASE="nix-daemon.service" # any time this is changed, it should be added above as a legacy service name
	readonly SERVICE_SRC=/lib/systemd/system/$NIX_DAEMON_SERVICE_BASE
	readonly SERVICE_DEST=/etc/systemd/system/$NIX_DAEMON_SERVICE_BASE

	readonly NIX_DAEMON_SOCKET_BASE="nix-daemon.socket" # any time this is changed, it should be added above as a legacy service name
	readonly SOCKET_SRC=/lib/systemd/system/$NIX_DAEMON_SOCKET_BASE
	readonly SOCKET_DEST=/etc/systemd/system/$NIX_DAEMON_SOCKET_BASE

	readonly TMPFILES_SRC=/lib/tmpfiles.d/nix-daemon.conf
	readonly TMPFILES_DEST=/etc/tmpfiles.d/nix-daemon.conf

	# Path for the systemd override unit file to contain the proxy settings
	readonly SERVICE_OVERRIDE=${SERVICE_DEST}.d/override.conf

# 
# systemd helpers
# 
	create_systemd_override() {
		header "Configuring proxy for the nix-daemon service"
		_sudo "create directory for systemd unit override" mkdir -p "$(dirname "$SERVICE_OVERRIDE")"
		cat <<-EOF | _sudo "create systemd unit override" tee "$SERVICE_OVERRIDE"
		[Service]
		$1
		EOF
	}

	# Gather all non-empty proxy environment variables into a string
	create_systemd_proxy_env() {
		vars="http_proxy https_proxy ftp_proxy no_proxy HTTP_PROXY HTTPS_PROXY FTP_PROXY NO_PROXY"
		for v in $vars; do
			if [ "x${!v:-}" != "x" ]; then
				echo "Environment=${v}=${!v}"
			fi
		done
	}

	handle_network_proxy() {
		# Create a systemd unit override with proxy environment variables
		# if any proxy environment variables are not empty.
		PROXY_ENV_STRING=$(create_systemd_proxy_env)
		if [ -n "${PROXY_ENV_STRING}" ]; then
			create_systemd_override "${PROXY_ENV_STRING}"
		fi
	}

	remove_service () {
		local service="$1"
		# if systemctl exists
		if [ -n "$(command -v "systemctl")" ]
		then
			_sudo "" systemctl stop "$service"
			_sudo "" systemctl disable "$service"
			_sudo "" rm -f /etc/systemd/system/"$service"
			_sudo "" rm -f /etc/systemd/system/"$service" # and symlinks that might be related
			_sudo "" rm -f /usr/lib/systemd/system/"$service" 
			_sudo "" rm -f /usr/lib/systemd/system/"$service" # and symlinks that might be related
			_sudo "" systemctl daemon-reload
			_sudo "" systemctl reset-failed
		fi
	}

# 
# shared interface implementation
# 
	poly_service_installed_check() {
		[ "$(systemctl is-enabled $NIX_DAEMON_SERVICE_BASE)" = "linked" ] \
			|| [ "$(systemctl is-enabled $NIX_DAEMON_SOCKET_BASE)" = "enabled" ]
	}

	poly_service_uninstall_directions() {
		cat <<-EOF
		$1. Delete the systemd service and socket units

		sudo systemctl stop $NIX_DAEMON_SOCKET_BASE
		sudo systemctl stop $NIX_DAEMON_SERVICE_BASE
		sudo systemctl disable $NIX_DAEMON_SOCKET_BASE
		sudo systemctl disable $NIX_DAEMON_SERVICE_BASE
		sudo systemctl daemon-reload
		EOF
	}

	poly_uninstall_directions() {
		subheader "Uninstalling nix:"
		local step=0

		if poly_service_installed_check; then
			step=$((step + 1))
			poly_service_uninstall_directions "$step"
		fi

		for profile_target in "${PROFILE_TARGETS[@]}"; do
			if [ -e "$profile_target" ] && [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
				step=$((step + 1))
				cat <<-EOF
				$step. Restore $profile_target$PROFILE_BACKUP_SUFFIX back to $profile_target

				sudo mv $profile_target$PROFILE_BACKUP_SUFFIX $profile_target

				(after this one, you may need to re-open any terminals that were
				opened while it existed.)

				EOF
			fi
		done

		step=$((step + 1))
		cat <<-EOF
		$step. Delete the files Nix added to your system:

		sudo rm -rf /etc/nix $NIX_ROOT $ROOT_HOME/.nix-profile $ROOT_HOME/.nix-defexpr $ROOT_HOME/.nix-channels $HOME/.nix-profile $HOME/.nix-defexpr $HOME/.nix-channels

		and that is it.

		EOF
	}

	poly_group_exists() {
		getent group "$1" > /dev/null 2>&1
	}

	poly_group_id_get() {
		getent group "$1" | cut -d: -f3
	}

	poly_create_build_group() {
		_sudo "Create the Nix build group, $NIX_BUILD_GROUP_NAME" \
				groupadd -g "$NIX_BUILD_GROUP_ID" --system \
				"$NIX_BUILD_GROUP_NAME" >&2
	}

	poly_user_exists() {
		getent passwd "$1" > /dev/null 2>&1
	}

	poly_user_id_get() {
		getent passwd "$1" | cut -d: -f3
	}

	poly_user_hidden_get() {
		echo "1"
	}

	poly_user_hidden_set() {
		true
	}

	poly_user_home_get() {
		getent passwd "$1" | cut -d: -f6
	}

	poly_user_home_set() {
		_sudo "in order to give $1 a safe home directory" \
				usermod --home "$2" "$1"
	}

	poly_user_note_get() {
		getent passwd "$1" | cut -d: -f5
	}

	poly_user_note_set() {
		_sudo "in order to give $1 a useful comment" \
				usermod --comment "$2" "$1"
	}

	poly_user_shell_get() {
		getent passwd "$1" | cut -d: -f7
	}

	poly_user_shell_set() {
		_sudo "in order to prevent $1 from logging in" \
				usermod --shell "$2" "$1"
	}

	poly_user_in_group_check() {
		groups "$1" | grep -q "$2" > /dev/null 2>&1
	}

	poly_user_in_group_set() {
		_sudo "Add $1 to the $2 group"\
				usermod --append --groups "$2" "$1"
	}

	poly_user_primary_group_get() {
		getent passwd "$1" | cut -d: -f4
	}

	poly_user_primary_group_set() {
		_sudo "to let the nix daemon use this user for builds (this might seem redundant, but there are two concepts of group membership)" \
				usermod --gid "$2" "$1"

	}

	poly_create_build_user() {
		username=$1
		uid=$2
		builder_num=$3

		_sudo "Creating the Nix build user, $username" \
				useradd \
				--home-dir /var/empty \
				--comment "Nix build user $builder_num" \
				--gid "$NIX_BUILD_GROUP_ID" \
				--groups "$NIX_BUILD_GROUP_NAME" \
				--no-user-group \
				--system \
				--shell /sbin/nologin \
				--uid "$uid" \
				--password "!" \
				"$username"
	}

	poly_force_delete_user() {
		user="$1"
		# logs them out by locking the account
		_sudo "" passwd -l "$user" 2>/dev/null
		# kill all their processes
		_sudo "" pkill -KILL -u "$user" 2>/dev/null
		# kill all their cron jobs
		_sudo "" crontab -r -u "$user" 2>/dev/null
		# kill all their print jobs
		if [ -n "$(command -v "lprm")" ]
		then
			lprm -U "$user" 2>/dev/null
		fi
		# actually remove the user
		_sudo "" deluser --remove-home "$user" 2>/dev/null # debian
		_sudo "" userdel --remove "$user" 2>/dev/null # non-debian
	}

	poly_commands_needed_before_init_nix_shell() {
		if [ -e /run/systemd/system ]; then
			:
		else
			cat <<-EOF
			$ sudo nix-daemon
			EOF
		fi
	}

# 
# 
# main poly methods (in chronological order)
# 
# 
	poly_1_additional_welcome_information() {
		cat <<-EOF
		- load and start a service (at $SERVICE_DEST and $SOCKET_DEST) for nix-daemon

		EOF
	}

	poly_2_passive_remove_artifacts() {
		: # no action needed
	}

	poly_3_check_for_leftover_artifacts() {
		# 
		# gather information
		# 
		local nixenv_check_failed=""
		local backup_profiles_check_failed=""
		check_nixenv_command_doesnt_exist || nixenv_check_failed="true"
		check_backup_profiles_exist  || backup_profiles_check_failed="true"
		# <<<could insert Linux specific checks here>>>
		
		# 
		# aggregate & echo any issues
		# 
		if [ -n "$nixenv_check_failed$backup_profiles_check_failed" ]
		then
			[ "$nixenv_check_failed"          = "true" ] && message_nixenv_command_doesnt_exist
			[ "$backup_profiles_check_failed" = "true" ] && message_backup_profiles_exist
			
			return 1
		else
			return 0
		fi
	}

	poly_4_agressive_remove_artifacts() {
		# remove as much as possible, even if some commands fail
		# (restore set -e behavior at the end of this function)
		set +e
		
		header "Agressively removing previous nix install"
		
		# 
		# remove services
		# 
		subheader "Removing any services"
			remove_service $NIX_DAEMON_SERVICE_BASE
			remove_service $NIX_DAEMON_SOCKET_BASE
			remove_service $NIX_LEGACY_DAEMON1_BASE
		
		# 
		# delete group
		#
		subheader "Removing group(s)" 
			_sudo "" groupdel "$NIX_BUILD_GROUP_NAME" 2>/dev/null
		
		# 
		# delete users
		# 
		subheader "Removing user(s)"
			# cant use NIX_USER_COUNT since it could change relative to previous installs
			for username in $(awk -F: '{ print $1 }' /etc/passwd | grep -E '^'"$NIX_USER_PREFIX"'[0-9]+$'); do
				# remove the users
				poly_force_delete_user "$username"
			done

		# 
		# purge all files
		# 
		subheader "Removing all nix files" 
			# multiple commands are used because some (e.g. the mounted volumes) may fail, and shouldn't stop the other files from being removed
			_sudo "" rm -rf /etc/nix                2>/dev/null
			_sudo "" rm -rf "$NIX_ROOT"             2>/dev/null
			_sudo "" rm -rf /var/root/.nix-profile  2>/dev/null
			_sudo "" rm -rf /var/root/.nix-defexpr  2>/dev/null
			_sudo "" rm -rf /var/root/.nix-channels 2>/dev/null
			_sudo "" rm -rf "$HOME"/.nix-profile    2>/dev/null
			_sudo "" rm -rf "$HOME"/.nix-defexpr    2>/dev/null
			_sudo "" rm -rf "$HOME"/.nix-channels   2>/dev/null
		
		# 
		# restoring any shell files
		# 
		subheader "Restoring all shell files" 
			unsetup_profiles
		
		set -e # go back to all uncaught errors failing
	}

	poly_5_assumption_validation() {
		# 
		# gather information
		# 
		local system_d_check_failed=""
		if [ ! -e /run/systemd/system ]; then
			failed_check "/run/systemd/system does not exist"
			system_d_check_failed="true"
		else
			passed_check "/run/systemd/system exists"
		fi
		
		# 
		# create aggregate message
		# 
		if [ -n "$system_d_check_failed" ]
		then
			warning <<-EOF
			We did not detect systemd on your system. With a multi-user install
			without systemd you will have to manually configure your init system to
			launch the Nix daemon after installation.
			EOF
			if ! ui_confirm "Do you want to proceed with a multi-user installation?"; then
				failure <<-EOF
				You have aborted the installation.
				EOF
			fi
		fi
	}

	poly_6_prepare_to_install() {
		: # no action needed
	}

	poly_7_configure_nix_daemon_service() {
		if [ -e /run/systemd/system ]; then
			task "Setting up the nix-daemon systemd service"

			_sudo "to create the nix-daemon tmpfiles config" \
					ln -sfn /nix/var/nix/profiles/default/$TMPFILES_SRC $TMPFILES_DEST

			_sudo "to run systemd-tmpfiles once to pick that path up" \
				systemd-tmpfiles --create --prefix=/nix/var/nix

			_sudo "to set up the nix-daemon service" \
					systemctl link "/nix/var/nix/profiles/default$SERVICE_SRC"

			_sudo "to set up the nix-daemon socket service" \
					systemctl enable "/nix/var/nix/profiles/default$SOCKET_SRC"

			handle_network_proxy

			_sudo "to load the systemd unit for nix-daemon" \
					systemctl daemon-reload

			_sudo "to start the $NIX_DAEMON_SOCKET_BASE" \
					systemctl start $NIX_DAEMON_SOCKET_BASE

			_sudo "to start the $NIX_DAEMON_SERVICE_BASE" \
					systemctl restart $NIX_DAEMON_SERVICE_BASE
		else
			reminder "I don't support your init system yet; you may want to add nix-daemon manually."
		fi
	}