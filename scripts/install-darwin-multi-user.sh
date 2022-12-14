#!/usr/bin/env bash

# 
# setup
# 
	set -eu
	set -o pipefail

	readonly NIX_DAEMON_BASE="org.nixos.nix-daemon"
	readonly NIX_DAEMON_DEST="/Library/LaunchDaemons/$NIX_DAEMON_BASE.plist"
	readonly NIX_VOLUME_DAEMON_BASE="org.nixos.darwin-store"
	readonly NIX_LEGACY_DAEMON1_BASE="org.nixos.activate-system"
	# create by default; set 0 to DIY, use a symlink, etc.
	readonly NIX_VOLUME_CREATE=${NIX_VOLUME_CREATE:-1} # now default
	NIX_FIRST_BUILD_UID="301"
	NIX_BUILD_USER_NAME_TEMPLATE="_nixbld%d"

# 
# load create-darwin-volumn.sh
# 
	# caution: may update times on / if not run as normal non-root user
	read_only_root() {
		# this touch command ~should~ always produce an error
		# as of this change I confirmed /usr/bin/touch emits:
		# "touch: /: Operation not permitted" Monterey
		# "touch: /: Read-only file system" Catalina+ and Big Sur
		# "touch: /: Permission denied" Mojave
		# (not matching prefix for compat w/ coreutils touch in case using
		# an explicit path causes problems; its prefix differs)
		case "$(/usr/bin/touch / 2>&1)" in
			*"Read-only file system") # Catalina, Big Sur
				return 0
				;;
			*"Operation not permitted") # Monterey
				return 0
				;;
			*)
				return 1
				;;
		esac

		# Avoiding the slow semantic way to get this information (~330ms vs ~8ms)
		# unless using touch causes problems. Just in case, that approach is:
		# diskutil info -plist / | <find the Writable or WritableVolume keys>, i.e.
		# diskutil info -plist / | xmllint --xpath "name(/plist/dict/key[text()='Writable']/following-sibling::*[1])" -
	}

	if read_only_root && [ "$NIX_VOLUME_CREATE" = 1 ]; then
		should_create_volume() { return 0; }
	else
		should_create_volume() { return 1; }
	fi

	# shellcheck source=./create-darwin-volume.sh
	. "$EXTRACTED_NIX_PATH/create-darwin-volume.sh" "no-main"

# 
# MacOS specific helpers
# 
	dsclattr() {
		/usr/bin/dscl . -read "$1" \
			| /usr/bin/awk "/$2/ { print \$2 }"
	}

	test_nix_daemon_installed() {
		test -e "$NIX_DAEMON_DEST"
	}

# 
# shared interface implementation
# 
	poly_service_installed_check() {
		if should_create_volume; then
			test_nix_daemon_installed || test_nix_volume_mountd_installed
		else
			test_nix_daemon_installed
		fi
	}

	poly_service_uninstall_directions() {
		echo "$1. Remove macOS-specific components:"
		if should_create_volume && test_nix_volume_mountd_installed; then
			nix_volume_mountd_uninstall_directions
		fi
		if test_nix_daemon_installed; then
			nix_daemon_uninstall_directions
		fi
	}

	poly_uninstall_directions() {
		subheader "Uninstalling nix:"
		# this is a better alternative than the more-detailed but incorrect  correct/verified uninstall directions can be 
		echo "Looks like there are still some pieces of a previous Nix installation on your machine"
		echo "Please follow the instructions linked below to purge these peices, and then try again"
		echo "at installing Nix"
		echo "Here are the uninstall/pure instructions: https://nixos.org/manual/nix/stable/installation/installing-binary.html#macos"
	}

	poly_group_exists() {
		/usr/bin/dscl . -read "/Groups/$1" > /dev/null 2>&1
	}

	poly_group_id_get() {
		dsclattr "/Groups/$1" "PrimaryGroupID"
	}

	poly_create_build_group() {
		_sudo "Create the Nix build group, $NIX_BUILD_GROUP_NAME" \
				/usr/sbin/dseditgroup -o create \
				-r "Nix build group for nix-daemon" \
				-i "$NIX_BUILD_GROUP_ID" \
				"$NIX_BUILD_GROUP_NAME" >&2
	}

	poly_user_exists() {
		/usr/bin/dscl . -read "/Users/$1" > /dev/null 2>&1
	}

	poly_user_id_get() {
		dsclattr "/Users/$1" "UniqueID"
	}

	poly_user_hidden_get() {
		dsclattr "/Users/$1" "IsHidden"
	}

	poly_user_hidden_set() {
		_sudo "in order to make $1 a hidden user" \
				/usr/bin/dscl . -create "/Users/$1" "IsHidden" "1"
	}

	poly_user_home_get() {
		dsclattr "/Users/$1" "NFSHomeDirectory"
	}

	poly_user_home_set() {
		# This can trigger a permission prompt now:
		# "Terminal" would like to administer your computer. Administration can include modifying passwords, networking, and system settings.
		_sudo "in order to give $1 a safe home directory" \
				/usr/bin/dscl . -create "/Users/$1" "NFSHomeDirectory" "$2"
	}

	poly_user_note_get() {
		dsclattr "/Users/$1" "RealName"
	}

	poly_user_note_set() {
		_sudo "in order to give $username a useful note" \
				/usr/bin/dscl . -create "/Users/$1" "RealName" "$2"
	}

	poly_user_shell_get() {
		dsclattr "/Users/$1" "UserShell"
	}

	poly_user_shell_set() {
		_sudo "in order to give $1 a safe shell" \
				/usr/bin/dscl . -create "/Users/$1" "UserShell" "$2"
	}

	poly_user_in_group_check() {
		username=$1
		group=$2
		/usr/sbin/dseditgroup -o checkmember -m "$username" "$group" > /dev/null 2>&1
	}

	poly_user_in_group_set() {
		username=$1
		group=$2

		_sudo "Add $username to the $group group"\
				/usr/sbin/dseditgroup -o edit -t user \
				-a "$username" "$group"
	}

	poly_user_primary_group_get() {
		dsclattr "/Users/$1" "PrimaryGroupID"
	}

	poly_user_primary_group_set() {
		_sudo "to let the nix daemon use this user for builds (this might seem redundant, but there are two concepts of group membership)" \
				/usr/bin/dscl . -create "/Users/$1" "PrimaryGroupID" "$2"
	}

	poly_create_build_user() {
		username=$1
		uid=$2
		builder_num=$3

		_sudo "Creating the Nix build user (#$builder_num), $username" \
				/usr/bin/dscl . create "/Users/$username" \
				UniqueID "${uid}"
	}

# 
# 
# main poly methods (in chronological order)
# 
# 
	poly_1_service_setup_note() {
		if should_create_volume; then
			echo " - create a Nix volume and a LaunchDaemon to mount it"
		fi
		echo " - create a LaunchDaemon (at $NIX_DAEMON_DEST) for nix-daemon"
		echo ""
	}

	poly_2_passive_remove_artifacts() {
		if should_create_volume; then
			task "Fixing any leftover Nix volume state"
			cat <<-EOF
			Before I try to install, I'll check for any existing Nix volume config
			and ask for your permission to remove it (so that the installer can
			start fresh). I'll also ask for permission to fix any issues I spot.
			EOF
			cure_volumes
			remove_volume_artifacts
		fi
	}

	poly_3_check_for_leftover_artifacts() {
		nixenv_check_failed=""
		backup_profiles_check_failed=""
		# 
		# gather information
		# 
		nixenv_command_doesnt_exist_check || nixenv_check_failed="true"
		backup_profiles_dont_exist_check  || backup_profiles_check_failed="true"
		# <<<could insert MacOS specific checks here>>>
		
		# 
		# aggregate & echo any issues
		# 
		if [ -n "$nixenv_check_failed$backup_profiles_check_failed" ]
		then
			[ "$nixenv_check_failed"          = "true" ] && nixenv_command_exists_error
			[ "$backup_profiles_check_failed" = "true" ] && backup_profiles_dont_exist_check
			return 1
		else
			return 0
		fi
	}

	poly_4_agressive_remove_artifacts() {
		set +e # remove as much as possible, even if some commands fail
				# (restore set -e behavior at the end of this function)
		# 
		# detach mounted volumes
		# 
		subheader "Removing any mounted volumes"
			# if it was mounted
			if sudo diskutil list | grep 'Nix Store' 1>/dev/null; then
				# it was removed successfully
				if sudo diskutil apfs deleteVolume "$NIX_ROOT"; then
					_sudo diskutil apfs deleteVolume "$NIX_ROOT" && _sudo rm -rf "$NIX_ROOT"
				fi
			fi
			
			# check if file exists
			mount_filepath="/etc/synthetic.conf"
			if [ -f "$mount_filepath" ]
			then
				echo "removing $mount_filepath"
				# if file contains "nix"
				if cat "$mount_filepath" | grep 'nix' 1>/dev/null
				then
					# remove nix from the file
					_sudo mount_filepath="$mount_filepath" -- bash -c '
						sudo cat "$mount_filepath" | sed -E '"'"'s/nix\n?$//g'"'"' > "$mount_filepath"
					'
				fi
			fi
			
			# check if file exists
			if [ -f "/etc/fstab" ]; then
				# absolute paths are used because this operation could be sensitive to BSD vs GNU implementations
				# would probably be good to check that all exist and are executable in the future
				write_to_fstab() {
					new_fstab_lines="$0"
					# vifs must be used to edit fstab
					# to make that work we  create a patch using "diff"
					# then tell vifs to use "patch" as an editor and apply the patch
					/usr/bin/diff /etc/fstab <(/usr/bin/printf "%s" "$new_fstab_lines") | EDITOR="/usr/bin/patch" _sudo /usr/sbin/vifs
				}
				if /usr/bin/grep "$NIX_ROOT apfs rw" /etc/fstab; then
					echo "Patching fstab"
					fstab_without_nix="$(/usr/bin/grep -v "$NIX_ROOT apfs rw" /etc/fstab)"
					write_to_fstab "$fstab_without_nix"
				fi
			fi
			
		# 
		# remove services
		# 
		subheader "Removing any services"
			remove_service() {
				# check if file exists
				if [ -f "/Library/LaunchDaemon/$1.plist" ]
				then
					echo "removing LaunchDaemon $1"
					_sudo launchctl bootout "system/$1" 2>/dev/null 
					_sudo launchctl unload "/Library/LaunchDaemon/$1.plist"
					_sudo rm "/Library/LaunchDaemons/$1.plist"
				fi
			}
			remove_service "$NIX_DAEMON_BASE"
			remove_service "$NIX_VOLUME_DAEMON_BASE"
			remove_service "$NIX_LEGACY_DAEMON1_BASE"
		
		# 
		# delete group
		#
		subheader "Removing group(s)" 
			_sudo groupdel nixbld 2>/dev/null
		
		# 
		# delete users
		# 
		subheader "Removing user(s)" 
			delete_user() {
				user="$1"
				# logs them out by locking the account
				_sudo passwd -l "$user" 2>/dev/null
				# kill all their processes
				_sudo pkill -KILL -u "$user" 2>/dev/null
				# kill all their cron jobs
				_sudo crontab -r -u "$user" 2>/dev/null
				# kill all their print jobs
				if [ -n "$(command -v "lprm")" ]
				then
					lprm -U "$user" 2>/dev/null
				fi
				# actually remove the user
				_sudo deluser --remove-home "$user" 2>/dev/null # debian
				_sudo userdel --remove "$user" 2>/dev/null # non-debian
			}
			
			# attempt 1
			for each_user in $(_sudo dscl . -list /Users | grep _nixbld); do _sudo dscl . -delete "/Users/$each_user"; done
			# attempt 2 (just for redundancy/fallback)
			for i in $(seq 1 "$NIX_USER_COUNT"); do
				# remove the users
				delete_user "$(nix_user_for_core "$i")"
			done

		# 
		# purge all files
		# 
		subheader "Removing all nix files" 
			# multiple commands are used because some (e.g. the mounted volumes) may fail, and shouldn't stop the other files from being removed
			_sudo rm -rf /etc/nix                2>/dev/null
			_sudo rm -rf "$NIX_ROOT"             2>/dev/null
			_sudo rm -rf /var/root/.nix-profile  2>/dev/null
			_sudo rm -rf /var/root/.nix-defexpr  2>/dev/null
			_sudo rm -rf /var/root/.nix-channels 2>/dev/null
			_sudo rm -rf "$HOME"/.nix-profile    2>/dev/null
			_sudo rm -rf "$HOME"/.nix-defexpr    2>/dev/null
			_sudo rm -rf "$HOME"/.nix-channels   2>/dev/null
		
		# 
		# restoring any shell files
		# 
		subheader "Restoring all shell files" 
			unsetup_profiles
		
		set -e # go back to all uncaught errors failing
		
		# MacOS mounted volume for /nix wont be deleted until reboot
		if [ -d "$NIX_ROOT" ]
		then
			failure <<-EOF

			Please reboot the machine and then re-run this script
			EOF
		fi
	}

	poly_5_assumption_validation() {
		:
	}

	poly_6_prepare_to_install() {
		if should_create_volume; then
			header "Preparing a Nix volume"
			# intentional indent below to match task indent
			cat <<-EOF
				Nix traditionally stores its data in the root directory $NIX_ROOT, but
				macOS now (starting in 10.15 Catalina) has a read-only root directory.
				To support Nix, I will create a volume and configure macOS to mount it
				at $NIX_ROOT.
			EOF
			setup_darwin_volume
		fi

		if [ "$(/usr/sbin/diskutil info -plist /nix | xmllint --xpath "(/plist/dict/key[text()='GlobalPermissionsEnabled'])/following-sibling::*[1]" -)" = "<false/>" ]; then
			failure "This script needs a /nix volume with global permissions! This may require running sudo /usr/sbin/diskutil enableOwnership /nix."
		fi
	}

	poly_7_configure_nix_daemon_service() {
		task "Setting up the nix-daemon LaunchDaemon"
		_sudo "to set up the nix-daemon as a LaunchDaemon" \
				/bin/cp -f "/nix/var/nix/profiles/default$NIX_DAEMON_DEST" "$NIX_DAEMON_DEST"

		_sudo "to load the LaunchDaemon plist for nix-daemon" \
				launchctl load /Library/LaunchDaemons/$NIX_DAEMON_BASE.plist

		_sudo "to start the nix-daemon" \
				launchctl kickstart -k system/$NIX_DAEMON_BASE
	}

	poly_8_extra_try_me_commands() {
		:
	}