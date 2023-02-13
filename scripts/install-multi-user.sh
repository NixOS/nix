#!/usr/bin/env bash

# 
# setup
# 
	set -eu
	set -o pipefail

	# Sourced from:
	# - https://github.com/LnL7/nix-darwin/blob/8c29d0985d74b4a990238497c47a2542a5616b3c/bootstrap.sh
	# - https://gist.github.com/expipiplus1/e571ce88c608a1e83547c918591b149f/ac504c6c1b96e65505fbda437a28ce563408ecb0
	# - https://github.com/NixOS/nixos-org-configurations/blob/a122f418797713d519aadf02e677fce0dc1cb446/delft/scripts/nix-mac-installer.sh
	# - https://github.com/matthewbauer/macNixOS/blob/f6045394f9153edea417be90c216788e754feaba/install-macNixOS.sh
	# - https://gist.github.com/LnL7/9717bd6cdcb30b086fd7f2093e5f8494/86b26f852ce563e973acd30f796a9a416248c34a
	#
	# however tracking which bits came from which would be impossible.

	readonly ESC='\033[0m'
	readonly BOLD='\033[1m'
	readonly BLUE='\033[34m'
	readonly BLUE_UL='\033[4;34m'
	readonly GREEN='\033[32m'
	readonly GREEN_UL='\033[4;32m'
	readonly RED='\033[31m'

	# installer allows overriding build user count to speed up installation
	# as creating each user takes non-trivial amount of time on macos
	readonly NIX_USER_COUNT=${NIX_USER_COUNT:-32}
	readonly NIX_BUILD_GROUP_ID="${NIX_BUILD_GROUP_ID:-30000}"
	# if this prefix changed, then poly_4_agressive_remove_artifacts() needs to be updated to include both the new and old group 
	readonly NIX_BUILD_GROUP_NAME="nixbld"
	# if this prefix changed, then poly_4_agressive_remove_artifacts() needs to be updated to include both the new and old prefix
	# additionally this should never include characters that are special when used in grep -E
	readonly NIX_USER_PREFIX="$NIX_BUILD_GROUP_NAME"
	# darwin installer needs to override these
	NIX_FIRST_BUILD_UID="${NIX_FIRST_BUILD_UID:-30001}"
	NIX_BUILD_USER_NAME_TEMPLATE="$NIX_USER_PREFIX%d"
	# Please don't change this. We don't support it, because the
	# default shell profile that comes with Nix doesn't support it.
	readonly NIX_ROOT="/nix"
	readonly NIX_EXTRA_CONF=${NIX_EXTRA_CONF:-}

	readonly PROFILE_NIX_START_DELIMETER="# Nix"
	readonly PROFILE_NIX_END_DELIMETER="# End Nix" # Caution, used both as a comment and inside grep argument (so $ or \+, etc would break things)
	readonly PROFILE_BACKUP_SUFFIX=".backup-before-nix"
	
	readonly NIX_INSTALLED_NIX="@nix@"
	readonly NIX_INSTALLED_CACERT="@cacert@"
	#readonly NIX_INSTALLED_NIX="/nix/store/j8dbv5w6jl34caywh2ygdy88knx1mdf7-nix-2.3.6"
	#readonly NIX_INSTALLED_CACERT="/nix/store/7dxhzymvy330i28ii676fl1pqwcahv2f-nss-cacert-3.49.2"
	readonly EXTRACTED_NIX_PATH="$(dirname "$0")"

	readonly ROOT_HOME=~root

	if [ -t 0 ] && [ -z "${NIX_INSTALLER_YES:-}" ]; then
		readonly IS_HEADLESS='no'
	else
		readonly IS_HEADLESS='yes'
	fi

# 
# shell support
# 
	# 
	# to add a shell
	# 
		# 1. fill out the api (shell name at end of each function)
			# shell_list_targets_SHELLNAME # tells user what files might be edited
			# shell_configure_profile_SHELLNAME
			# shell_check_backup_profile_exists_message_SHELLNAME
			# shell_unsetup_profiles_SHELLNAME
		# 2. update the "aggregated shell API" below to call the new functions
		
	# 
	# bash
	# 
		readonly PROFILE_TARGETS_BASH=("/etc/bashrc" "/etc/profile.d/nix.sh" "/etc/bash.bashrc")
		readonly PROFILE_NIX_FILE_BASH="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.sh"
		
		shell_list_targets_bash() {
			for profile_target in "${PROFILE_TARGETS_BASH[@]}"; do
				if [ -e "$profile_target" ]; then
					cat <<-EOF
					- back up $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX
					- update $profile_target to include some Nix configuration
					EOF
				fi
			done
		}
		
		_helper_source_lines_bash() {
			cat <<-EOF

			$PROFILE_NIX_START_DELIMETER
			if [ -e '$PROFILE_NIX_FILE_BASH' ]; then
			. '$PROFILE_NIX_FILE_BASH'
			fi
			$PROFILE_NIX_END_DELIMETER

			EOF
		}
		
		shell_configure_profile_bash() {
			task "Setting up shell profiles for Bash"
			for profile_target in "${PROFILE_TARGETS_BASH[@]}"; do
				if [ -e "$profile_target" ]; then
					_sudo "to back up your current $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX" \
						cp "$profile_target" "$profile_target$PROFILE_BACKUP_SUFFIX"
				else
					# try to create the file if its directory exists
					target_dir="$(dirname "$profile_target")"
					if [ -d "$target_dir" ]; then
						_sudo "to create a stub $profile_target which will be updated" \
							touch "$profile_target"
					fi
				fi

				if [ -e "$profile_target" ]; then
					_helper_source_lines_bash \
						| _sudo "extend your $profile_target with nix-daemon settings" \
								tee -a "$profile_target"
				fi
			done
		}
		
		shell_check_backup_profile_exists_message_bash() {
			local at_least_one_failed="false"
			for profile_target in "${PROFILE_TARGETS_BASH[@]}"; do
				if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
					# this backup process first released in Nix 2.1
					at_least_one_failed="true"
					failed_check "$profile_target$PROFILE_BACKUP_SUFFIX already exists"
				else
					passed_check "$profile_target$PROFILE_BACKUP_SUFFIX does not exist yet"
				fi
			done
			
			if [ "$at_least_one_failed" = "true" ]; then
				subheader "For Bash"
				profiles=""
				profile_backups=""
				for profile_target in "${PROFILE_TARGETS_BASH[@]}"; do
					if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
						profiles="$profiles, $profile_target"
						profile_backups="$profile_backups, $profile_target$PROFILE_BACKUP_SUFFIX"
					fi
				done
				
				cat <<-EOF
				I need to back up each of these profiles: $profiles
				To their respective backups: $profile_backups
				But those backup files already exist.

				Here's how to clean up the old bash backup files:

				1. Open each of these profiles, and look for something similar to the following
				$(_helper_source_lines_bash)

				2. Remove those lines that mention Nix, and save the file

				3. Move these backup files to a new location: $profile_backups
				(ideally a location you will remember encase you need to rollback to them)
				
				
				EOF
			fi
		}
		
		shell_unsetup_profiles_bash() {
			for profile_target in "${PROFILE_TARGETS_BASH[@]}"; do
				# removes everything between $PROFILE_NIX_START_DELIMETER and $PROFILE_NIX_END_DELIMETER
				# also has "something seems weird" checks that prompt manual editing
				restore_profile "$profile_target"
			done
		}
		
	# 
	# zsh
	# 
		readonly PROFILE_ZSH_TARGETS=("/etc/zshrc" "/etc/zsh/zshrc")
		readonly PROFILE_NIX_FILE_ZSH="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.sh"
		shell_list_targets_zsh() {
			for profile_target in "${PROFILE_TARGETS_ZSH[@]}"; do
				if [ -e "$profile_target" ]; then
					cat <<-EOF
					- back up $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX
					- update $profile_target to include some Nix configuration
					EOF
				fi
			done
		}
		
		_helper_source_lines_zsh() {
			cat <<-EOF

			$PROFILE_NIX_START_DELIMETER
			if [ -e '$PROFILE_NIX_FILE_ZSH' ]; then
			. '$PROFILE_NIX_FILE_ZSH'
			fi
			$PROFILE_NIX_END_DELIMETER

			EOF
		}
		
		shell_configure_profile_zsh() {
			task "Setting up shell profiles for Zsh"
			for profile_target in "${PROFILE_TARGETS_ZSH[@]}"; do
				if [ -e "$profile_target" ]; then
					_sudo "to back up your current $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX" \
						cp "$profile_target" "$profile_target$PROFILE_BACKUP_SUFFIX"
				else
					# try to create the file if its directory exists
					target_dir="$(dirname "$profile_target")"
					if [ -d "$target_dir" ]; then
						_sudo "to create a stub $profile_target which will be updated" \
							touch "$profile_target"
					fi
				fi

				if [ -e "$profile_target" ]; then
					_helper_source_lines_zsh \
						| _sudo "extend your $profile_target with nix-daemon settings" \
								tee -a "$profile_target"
				fi
			done
		}
		
		shell_check_backup_profile_exists_message_zsh() {
			local at_least_one_failed="false"
			for profile_target in "${PROFILE_TARGETS_ZSH[@]}"; do
				if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
					# this backup process first released in Nix 2.1
					at_least_one_failed="true"
					failed_check "$profile_target$PROFILE_BACKUP_SUFFIX already exists"
				else
					passed_check "$profile_target$PROFILE_BACKUP_SUFFIX does not exist yet"
				fi
			done
			
			if [ "$at_least_one_failed" = "true" ]; then
				subheader "For Zsh"
				profiles=""
				profile_backups=""
				for profile_target in "${PROFILE_TARGETS_ZSH[@]}"; do
					if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
						profiles="$profiles, $profile_target"
						profile_backups="$profile_backups, $profile_target$PROFILE_BACKUP_SUFFIX"
					fi
				done
				
				cat <<-EOF
				I need to back up each of these profiles: $profiles
				To their respective backups: $profile_backups
				But those backup files already exist.

				Here's how to clean up the old zsh backup files:

				1. Open each of these profiles, and look for something similar to the following
				$(_helper_source_lines_zsh)

				2. Remove those lines that mention Nix, and save the file

				3. Move these backup files to a new location: $profile_backups
				(ideally a location you will remember encase you need to rollback to them)
				
				
				EOF
			fi
		}
		
		shell_unsetup_profiles_zsh() {
			for profile_target in "${PROFILE_TARGETS_ZSH[@]}"; do
				# removes everything between $PROFILE_NIX_START_DELIMETER and $PROFILE_NIX_END_DELIMETER
				# also has "something seems weird" checks that prompt manual editing
				restore_profile "$profile_target"
			done
		}
	
	# 
	# fish
	# 
		readonly PROFILE_SUFFIX_FISH="conf.d/nix.fish"
		readonly PROFILE_PREFIXES_FISH=(
			# each of these are common values of $__fish_sysconf_dir,
			# under which Fish will look for a file named
			# $PROFILE_SUFFIX_FISH.
			"/etc/fish"              # standard
			"/usr/local/etc/fish"    # their installer .pkg for macOS
			"/opt/homebrew/etc/fish" # homebrew
			"/opt/local/etc/fish"    # macports
		)
		readonly PROFILE_TARGETS_FISH=(
			"/etc/fish/conf.d/nix.fish"              
			"/usr/local/etc/fish/conf.d/nix.fish"    
			"/opt/homebrew/etc/fish/conf.d/nix.fish" 
			"/opt/local/etc/fish/conf.d/nix.fish"    
		)
		readonly PROFILE_NIX_FILE_FISH="$NIX_ROOT/var/nix/profiles/default/etc/profile.d/nix-daemon.fish"
		
		shell_list_targets_fish() {
			for profile_target in "${PROFILE_NIX_FILE_FISH[@]}"; do
				if [ -e "$profile_target" ]; then
					cat <<-EOF
					- back up $profile_target to $profile_target$PROFILE_BACKUP_SUFFIX
					- update $profile_target to include some Nix configuration
					EOF
				fi
			done
		}
		
		_helper_source_lines_fish() {
			cat <<-EOF
			
			$PROFILE_NIX_START_DELIMETER
			if test -e '$PROFILE_NIX_FILE_FISH'
			. '$PROFILE_NIX_FILE_FISH'
			end
			$PROFILE_NIX_END_DELIMETER
			
			EOF
		}
		
		shell_configure_profile_fish() {
			task "Setting up shell profiles for Fish with with ${PROFILE_SUFFIX_FISH} inside ${PROFILE_PREFIXES_FISH[*]}"
			for fish_prefix in "${PROFILE_PREFIXES_FISH[@]}"; do
				if [ ! -d "$fish_prefix" ]; then
					# this specific prefix (ie: /etc/fish) is very likely to exist
					# if Fish is installed with this sysconfdir.
					continue
				fi

				profile_target="${fish_prefix}/${PROFILE_SUFFIX_FISH}"
				conf_dir=$(dirname "$profile_target")
				if [ ! -d "$conf_dir" ]; then
					_sudo "create $conf_dir for our Fish hook" \
						mkdir "$conf_dir"
				fi

				_helper_source_lines_fish \
					| _sudo "write nix-daemon settings to $profile_target" \
							tee "$profile_target"
			done
		}
		
		shell_check_backup_profile_exists_message_fish() {
			local at_least_one_failed="false"
			for profile_target in "${PROFILE_TARGETS_FISH[@]}"; do
				if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
					# this backup process first released in Nix 2.1
					at_least_one_failed="true"
					failed_check "$profile_target$PROFILE_BACKUP_SUFFIX already exists"
				else
					passed_check "$profile_target$PROFILE_BACKUP_SUFFIX does not exist yet"
				fi
			done
			
			if [ "$at_least_one_failed" = "true" ]; then
				subheader "For Zsh"
				profiles=""
				profile_backups=""
				for profile_target in "${PROFILE_TARGETS_FISH[@]}"; do
					if [ -e "$profile_target$PROFILE_BACKUP_SUFFIX" ]; then
						profiles="$profiles, $profile_target"
						profile_backups="$profile_backups, $profile_target$PROFILE_BACKUP_SUFFIX"
					fi
				done
				
				cat <<-EOF
				I need to back up each of these profiles: $profiles
				To their respective backups: $profile_backups
				But those backup files already exist.

				Here's how to clean up the old bash backup files:

				1. Open each of these profiles, and look for something similar to the following
				$(_helper_source_lines_fish)

				2. Remove those lines that mention Nix, and save the file

				3. Move these backup files to a new location: $profile_backups
				(ideally a location you will remember encase you need to rollback to them)
				
				
				EOF
			fi
		}
		
		shell_unsetup_profiles_fish() {
			for profile_target in "${PROFILE_TARGETS_FISH[@]}"; do
				# removes everything between $PROFILE_NIX_START_DELIMETER and $PROFILE_NIX_END_DELIMETER
				# also has "something seems weird" checks that prompt manual editing
				restore_profile "$profile_target"
			done
		}
		
	# 
	# aggregated shell API
	# 
		shell_list_targets() {
			shell_list_targets_bash
			shell_list_targets_zsh
			shell_list_targets_fish
		}
		
		shell_configure_profile() {
			shell_configure_profile_bash
			shell_configure_profile_zsh
			shell_configure_profile_fish
		}
		
		shell_backup_profiles_exist_message() {
			aggregated_message=""
			aggregated_message="$aggregated_message$(shell_check_backup_profile_exists_message_bash)"
			aggregated_message="$aggregated_message$(shell_check_backup_profile_exists_message_zsh)"
			aggregated_message="$aggregated_message$(shell_check_backup_profile_exists_message_fish)"
			# if any of them error, report it
			if [ -n "$aggregated_message" ]
			then
				echo "$aggregated_message" | error
			fi
		}
		
		shell_unsetup_profiles() {
			shell_unsetup_profiles_bash
			shell_unsetup_profiles_zsh
			shell_unsetup_profiles_fish
		}

# 
# generic global helpers
# 
	headless() {
		if [ "$IS_HEADLESS" = "yes" ]; then
			return 0
		else
			return 1
		fi
	}

	is_root() {
		if [ "$EUID" -eq 0 ]; then
			return 0
		else
			return 1
		fi
	}
	
	is_os_selinux() {
		if command -v getenforce > /dev/null 2>&1; then
			if [ "$(getenforce)" = "Enforcing" ]; then
				return 0
			fi
		fi
		return 1
	}

	is_os_linux() {
		if [ "$(uname -s)" = "Linux" ]; then
			return 0
		else
			return 1
		fi
	}

	is_os_darwin() {
		if [ "$(uname -s)" = "Darwin" ]; then
			return 0
		else
			return 1
		fi
	}

	_textout() {
		echo -en "$1"
		shift
		if [ "$*" = "" ]; then
			cat
		else
			echo "$@"
		fi
		echo -en "$ESC"
	}

	header() {
		follow="---------------------------------------------------------"
		header=$(echo "---- $* $follow$follow$follow" | head -c 80)
		echo ""
		_textout "$BLUE" "$header"
	}

	warningheader() {
		follow="---------------------------------------------------------"
		header=$(echo "---- $* $follow$follow$follow" | head -c 80)
		echo ""
		_textout "$RED" "$header"
	}

	subheader() {
		echo ""
		_textout "$BLUE_UL" "$*"
	}

	row() {
		printf "$BOLD%s$ESC:\\t%s\\n" "$1" "$2"
	}

	task() {
		echo ""
		ok "~~> $1"
	}

	bold() {
		echo "$BOLD$*$ESC"
	}

	ok() {
		_textout "$GREEN" "$@"
	}

	warning() {
		warningheader "warning!"
		cat
		echo ""
	}

	error() { # used as one-part of a mutli-part failure message
		warningheader "error!"
		cat
		echo ""
	}

	stop() {
		echo "$1"
		exit 0
	}

	failure() {
		header "oh no!"
		_textout "$RED" "$@"
		echo ""
		_textout "$RED" "$(get_help)"
		trap finish_cleanup EXIT
		exit 1
	}

	failed_check() {
		_textout "$RED" "    ! : $@"
		return 1
	}

	passed_check() {
		_textout "$GREEN" "    . : $@"
		return 0
	}

	ui_confirm() {
		_textout "$GREEN$GREEN_UL" "$1"

		if headless; then
			echo "No TTY, assuming you would say yes :)"
			return 0
		fi

		local prompt="[y/n] "
		echo -n "$prompt"
		while read -r y; do
			if [ "$y" = "y" ]; then
				echo ""
				return 0
			elif [ "$y" = "n" ]; then
				echo ""
				return 1
			else
				_textout "$RED" "Sorry, I didn't understand. I can only understand answers of y or n"
				echo -n "$prompt"
			fi
		done
		echo ""
		return 1
	}

	printf -v _UNCHANGED_GRP_FMT "%b" $'\033[2m%='"$ESC" # "dim"
	# bold+invert+red and bold+invert+green just for the +/- below
	# red/green foreground for rest of the line
	printf -v _OLD_LINE_FMT "%b" $'\033[1;7;31m-'"$ESC ${RED}%L${ESC}"
	printf -v _NEW_LINE_FMT "%b" $'\033[1;7;32m+'"$ESC ${GREEN}%L${ESC}"

	_diff() {
		# simple colorized diff comatible w/ pre `--color` versions
		diff --unchanged-group-format="$_UNCHANGED_GRP_FMT" --old-line-format="$_OLD_LINE_FMT" --new-line-format="$_NEW_LINE_FMT" --unchanged-line-format="  %L" "$@"
	}

	confirm_rm() {
		local path="$1"
		if ui_confirm "Can I remove $path?"; then
			_sudo "to remove $path" rm "$path"
		fi
	}

	_SERIOUS_BUSINESS="${RED}%s:${ESC} "
	password_confirm() {
		local do_something_consequential="$1"
		if ui_confirm "Can I $do_something_consequential?"; then
			# shellcheck disable=SC2059
			sudo -kv --prompt="$(printf "${_SERIOUS_BUSINESS}" "Enter your password to $do_something_consequential")"
		else
			return 1
		fi
	}

	# Support accumulating reminders over the course of a run and showing
	# them at the end. An example where this helps: the installer changes
	# something, but it won't work without a reboot. If you tell the user
	# when you do it, they may miss it in the stream. The value of the
	# setting isn't enough to decide whether to message because you only
	# need to message if you *changed* it.

	# reminders stored in array delimited by empty entry; if ! headless,
	# user is asked to confirm after each delimiter.
	_reminders=()
	((_remind_num=1))

	remind() {
		# (( arithmetic expression ))
		if (( _remind_num > 1 )); then
			header "Reminders"
			for line in "${_reminders[@]}"; do
				echo "$line"
				if ! headless && [ "${#line}" = 0 ]; then
					if read -r -p "Press enter/return to acknowledge."; then
						printf $'\033[A\33[2K\r'
					fi
				fi
			done
		fi
	}

	reminder() {
		printf -v label "${BLUE}[ %d ]${ESC}" "$_remind_num"
		_reminders+=("$label")
		if [[ "$*" = "" ]]; then
			while read -r line; do
				_reminders+=("$line")
			done
		else
			# this expands each arg to an array entry (and each entry will
			# ultimately be a separate line in the output)
			_reminders+=("$@")
		fi
		_reminders+=("")
		((_remind_num++))
	}

	__sudo() {
		local expl="$1"
		local cmd="$2"
		shift
		header "sudo execution"

		echo "I am executing:"
		echo ""
		printf "    $ sudo %s\\n" "$cmd"
		echo ""
		echo "$expl"
		echo ""

		return 0
	}

	_sudo() {
		local expl="$1"
		shift
		if ! headless || is_root; then
			__sudo "$expl" "$*" >&2
		fi

		if is_root; then
			env "$@"
		else
			sudo "$@"
		fi
	}

	command_exists() {
		[ -n "$(command -v "$1")" ]
	}

	# Ensure that $TMPDIR exists if defined.
	if [[ -n "${TMPDIR:-}" ]] && [[ ! -d "${TMPDIR:-}" ]]; then
		mkdir -m 0700 -p "${TMPDIR:-}"
	fi

	readonly SCRATCH=$(mktemp -d)
	finish_cleanup() {
		rm -rf "$SCRATCH"
	}

	finish_fail() {
		finish_cleanup

		failure <<-EOF
		Oh no, something went wrong. If you can take all the output and open
		an issue, we'd love to fix the problem so nobody else has this issue.

		:(
		EOF
	}
	trap finish_fail EXIT

	contact_us() {
		echo "You can open an issue at"
		echo "https://github.com/NixOS/nix/issues/new?labels=installer&template=installer.md"
		echo ""
		echo "Or get in touch with the community: https://nixos.org/community"
	}

	get_help() {
		echo "We'd love to help if you need it."
		echo ""
		contact_us
	}

# 
# nix-specific helpers
# 
	nix_user_for_core() {
		printf "$NIX_BUILD_USER_NAME_TEMPLATE" "$1"
	}

	nix_uid_for_core() {
		echo $((NIX_FIRST_BUILD_UID + $1 - 1))
	}

	confirm_edit() {
		local path="$1"
		local edit_path="$2"
		cat <<-EOF

		Nix isn't the only thing in $path,
		but I think I know how to edit it out.
		Here's the diff:
		EOF

		# could technically test the diff, but caller should do it
		_diff "$path" "$edit_path"
		if ui_confirm "Does the change above look right?"; then
			_sudo "remove nix from $path" cp "$edit_path" "$path"
		fi
	}

	finish_success() {
		ok "Alright! We're done!"

		cat <<-EOF
		Try it! Open a new terminal, and type:
		$(poly_commands_needed_before_init_nix_shell)
		$ nix-shell -p nix-info --run "nix-info -m"

		Thank you for using this installer. If you have any feedback or need
		help, don't hesitate:

		$(contact_us)
		EOF
		remind
		finish_cleanup
	}

	finish_uninstall_success() {
		ok "Alright! Nix should be removed!"

		cat <<-EOF
		If you spot anything this uninstaller missed or have feedback,
		don't hesitate:

		$(contact_us)
		EOF
		remind
		finish_cleanup
	}

	unsetup_profiles_needs_manual_editing() {
		local profile="$1"
		echo "Something is really messed up with your $profile file"
		echo "I think you need to manually edit it to remove everything related to Nix"
		# check backup exists
		if [ -f "$profile.backup-before-nix" ]; then
			echo "NOTE: you do have a backup file $profile.backup-before-nix"
			echo "So you might want to use that for reference"
		fi
	}

	extract_nix_profile_injection() {
		profile="$1"
		start_line_number="$(cat "$profile" | grep -n "$PROFILE_NIX_START_DELIMETER"'$' | cut -f1 -d: | head -n1)"
		end_line_number="$(cat "$profile" | grep -n "$PROFILE_NIX_END_DELIMETER"'$' | cut -f1 -d: | head -n1)"
		if [ -n "$start_line_number" ] && [ -n "$end_line_number" ]; then
			if [ $start_line_number -gt $end_line_number ]; then
				line_number_before=$(( $start_line_number - 1 ))
				line_number_after=$(( $end_line_number + 1))
				new_top_half="$(head -n$line_number_before)
				"
				new_profile="$new_top_half$(tail -n "+$line_number_after")"
				# overwrite existing profile, but with only Nix removed
				printf '%s' "$new_profile" | _sudo "" tee "$profile" 1>/dev/null
				return 0
			else 
				unsetup_profiles_needs_manual_editing "$profile"
				return 1
			fi
		elif [ -n "$start_line_number" ] || [ -n "$end_line_number" ]; then
			unsetup_profiles_needs_manual_editing "$profile"
			return 1
		fi
	}

	restore_profile() {
		profile="$1"
		
		# check if file exists
		if [ -f "$profile" ]; then
			if extract_nix_profile_injection "$profile"; then
				# the extraction is done in-place. 
				# this is safer than restoring a backup because its possible
				# other non-nix tools have added things, and restoring the backup would remove those
				# if the extraction was 
				_sudo "" rm -f "$profile.backup-before-nix"
			fi
		fi
	}

	check_nixenv_command_doesnt_exist() {
		if command_exists "nix-env"; then
			failed_check "nix-env command already exists"
		else
			passed_check "no previous nix-env found"
		fi
	}

	message_nixenv_command_doesnt_exist() {
		echo
		error <<-EOF
		Nix already appears to be installed. This installer may run into issues.
		If an error occurs, try manually uninstalling, then rerunning this script.
		EOF
	}

	unsetup_profiles_needs_manual_editing() {
		local profile="$1"
		echo "Something is really messed up with your $profile file"
		echo "I think you need to manually edit it to remove everything related to Nix"
		# check backup exists
		if [ -f "$profile.backup-before-nix" ]; then
			echo "NOTE: you do have a backup file $profile.backup-before-nix"
			echo "So you might want to use that for reference"
		fi
	}

	extract_nix_profile_injection() {
		profile="$1"
		start_line_number="$(cat "$profile" | grep -n "$PROFILE_NIX_START_DELIMETER"'$' | cut -f1 -d: | head -n1)"
		end_line_number="$(cat "$profile" | grep -n "$PROFILE_NIX_END_DELIMETER"'$' | cut -f1 -d: | head -n1)"
		if [ -n "$start_line_number" ] && [ -n "$end_line_number" ]; then
			if [ $start_line_number -gt $end_line_number ]; then
				line_number_before=$(( $start_line_number - 1 ))
				line_number_after=$(( $end_line_number + 1))
				new_top_half="$(head -n$line_number_before)
				"
				new_profile="$new_top_half$(tail -n "+$line_number_after")"
				# overwrite existing profile, but with only Nix removed
				printf '%s' "$new_profile" | _sudo "" tee "$profile" 1>/dev/null
				return 0
			else 
				unsetup_profiles_needs_manual_editing "$profile"
				return 1
			fi
		elif [ -n "$start_line_number" ] || [ -n "$end_line_number" ]; then
			unsetup_profiles_needs_manual_editing "$profile"
			return 1
		fi
	}
	
	restore_profile() {
		profile="$1"
		
		# check if file exists
		if [ -f "$profile" ]; then
			if extract_nix_profile_injection "$profile"; then
				# the extraction is done in-place. 
				# this is safer than restoring a backup because its possible
				# other non-nix tools have added things, and restoring the backup would remove those
				# if the extraction was successful, remove the backup-before-nix
				_sudo "" rm -f "$profile.backup-before-nix"
			fi
		fi
	}
# 
# main
# 
	_would_like_more_information="false"
	_should_aggresive_remove_artifacts="false"
	main() {
		# 
		# load poly interface
		# 
		# Interface is the following functions: 
		#     poly_1_additional_welcome_information
		#     poly_2_passive_remove_artifacts
		#     poly_3_check_for_leftover_artifacts
		#     poly_4_agressive_remove_artifacts
		#     poly_5_assumption_validation
		#     poly_6_prepare_to_install
		#     poly_7_configure_nix_daemon_service
		#     poly_commands_needed_before_init_nix_shell
		#     poly_create_build_group
		#     poly_create_build_user
		#     poly_group_exists
		#     poly_group_id_get
		#     poly_service_installed_check
		#     poly_service_uninstall_directions
		#     poly_uninstall_directions
		#     poly_user_exists
		#     poly_user_hidden_get
		#     poly_user_hidden_set
		#     poly_user_home_get
		#     poly_user_home_set
		#     poly_user_id_get
		#     poly_user_in_group_check
		#     poly_user_in_group_set
		#     poly_user_note_get
		#     poly_user_note_set
		#     poly_user_primary_group_get
		#     poly_user_primary_group_set
		#     poly_user_shell_get
		#     poly_user_shell_set
		if is_os_selinux; then
			failure <<-EOF
			Nix does not work with selinux enabled yet!
			see https://github.com/NixOS/nix/issues/2374
			EOF
		elif is_os_darwin; then
			# shellcheck source=./install-darwin-multi-user.sh
			. "$EXTRACTED_NIX_PATH/install-darwin-multi-user.sh"
		elif is_os_linux; then
			# shellcheck source=./install-systemd-multi-user.sh
			. "$EXTRACTED_NIX_PATH/install-systemd-multi-user.sh" # most of this works on non-systemd distros also
		else
			failure "Sorry, I don't know what to do on $(uname)"
		fi
		
		
		# 
		# introduction
		# 
		welcome_to_nix
		[ "$_would_like_more_information" = "true" ] && poly_1_additional_welcome_information
		[ "$_would_like_more_information" = "true" ] && additional_info_confirm_prompt
		chat_about_sudo_if_needed
		
		# 
		# saftey checks
		# 
		poly_2_passive_remove_artifacts
		message_about_artifacts
		# TODO: there's a tension between cure and validate. I moved the
		# the sudo/root check out of validate to the head of this func.
		# Cure is *intended* to subsume the validate-and-abort approach,
		# so it may eventually obsolete it.
		poly_3_check_for_leftover_artifacts || leftovers_detected_conversation
		[ "$_should_aggresive_remove_artifacts" = "true" ] && { poly_4_agressive_remove_artifacts; }
		# even if removal is successful, re-run the script so that ENV vars are refreshed with the changes
		[ "$_should_aggresive_remove_artifacts" = "true" ] && stop "Please re-run script so the purge will take effect"
		poly_5_assumption_validation
		setup_report_confirm
		
		# 
		# installation
		# 
		poly_6_prepare_to_install
		create_build_group
		create_build_users
		create_directories
		place_channel_configuration
		install_from_extracted_nix
		shell_configure_profile
		set +eu # allow errors when sourcing the profile
		# shellcheck disable=SC1091
		. /etc/profile
		set -eu
		setup_default_profile
		place_nix_configuration
		poly_7_configure_nix_daemon_service

		trap finish_success EXIT
	}

# 
# main helpers
# 
	welcome_to_nix() {
		ok "Welcome to the Multi-User Nix Installation"

		cat <<-EOF

		This installation tool will set up your computer with the Nix package
		manager. This will happen in a few stages:

		1. Make sure your computer doesn't already have Nix. If it does, I
		will show you instructions on how to clean up your old install.

		2. Show you what I am going to install and where. Then I will ask
		if you are ready to continue.

		3. Create the system users and groups that the Nix daemon uses to run
		builds.

		4. Perform the basic installation of the Nix files daemon.

		5. Configure your shell to import special Nix Profile files, so you
		can use Nix.

		6. Start the Nix daemon.

		EOF

		if ui_confirm "Would you like to see a more detailed list of what I will do?"; then
			_would_like_more_information="true"
			cat <<-EOF

			I will:

			- make sure your computer doesn't already have Nix files
			  (if it does, I will tell you how to clean them up.)
			- create local users (see the list above for the users I'll make)
			- create a local group ($NIX_BUILD_GROUP_NAME)
			- install Nix in to $NIX_ROOT
			- create a configuration file in /etc/nix
			- set up the "default profile" by creating some Nix-related files in
			  $ROOT_HOME
			EOF
			shell_list_targets
		fi
	}

	additional_info_confirm_prompt() {
		if ! ui_confirm "Ready to continue?"; then
			failure <<-EOF
			Okay, maybe you would like to talk to the team.
			EOF
		fi
	}

	chat_about_sudo_if_needed() {
		if ! is_root; then
			header "let's talk about sudo"

			if headless; then
				cat <<-EOF
				This script is going to call sudo a lot. Normally, it would show you
				exactly what commands it is running and why. However, the script is
				run in a headless fashion, like this:

				$ curl -L https://nixos.org/nix/install | sh

				or maybe in a CI pipeline. Because of that, I'm going to skip the
				verbose output in the interest of brevity.

				If you would like to
				see the output, try like this:

				$ curl -L -o install-nix https://nixos.org/nix/install
				$ sh ./install-nix

				EOF
				return 0
			fi

			cat <<-EOF
			This script is going to call sudo a lot. Every time I do, it'll
			output exactly what it'll do, and why.

			Just like this:
			EOF

			__sudo "to demonstrate how our sudo prompts look" \
				echo "this is a sudo prompt"

			cat <<-EOF

			This might look scary, but everything can be undone by running just a
			few commands. I used to ask you to confirm each time sudo ran, but it
			was too many times. Instead, I'll just ask you this one time:

			EOF
			if ui_confirm "Can I use sudo?"; then
				ok "Yay! Thanks! Let's get going!"
			else
				failure <<-EOF
				That is okay, but I can't install.
				EOF
			fi
		fi
	}

	message_about_artifacts() {
		task "Checking for artifacts of previous installs"
		cat <<-EOF
		Before I try to install, I'll check for signs Nix already is or has
		been installed on this system.
		EOF
	}

	leftovers_detected_conversation() {
		warningheader "Leftovers From Previous Install Detected"
		echo 
		echo 
		echo '    Please remove the leftovers (instructions above)'
		echo '    The instructions are customized your specific leftovers/system'
		echo '    Detailed instructions can be found at:'
		if is_os_linux; then
			echo "        https://nixos.org/manual/nix/stable/installation/installing-binary.html#linux"
		elif is_os_darwin; then
			echo "        https://nixos.org/manual/nix/stable/installation/installing-binary.html#macos"
		else
			echo "        https://nixos.org/manual/nix/stable/installation/installing-binary.html"
		fi
		
		echo
		ui_confirm 'Understood?' 
		# Presumably most people will attempt to manually remove leftovers before even getting to the un_confirm below
		# (which is intentional)
		
		# in all honesty the operation is pretty safe
		# the biggest risk is that, somehow, PROFILE_NIX_START_DELIMETER and PROFILE_NIX_END_DELIMETER are BOTH
		# present, in order, but for some reason there is important non-nix profile-commands inbetween them.
		# I can't imagine how/why that would happen, but people do crazy things with profiles
		if ui_confirm "Are you interested in a UNSAFE automatic purge of leftovers?"; then
			# this 2nd check is so the no-tty-scenario doesnt trigger something labelled as unsafe
			# maybe in the future when the agressive purge is throughly tested, it will make sense to have it as the default no-tty behavior
			if ! ui_confirm "Opposite-question check: do you want only safe operations to be completed"; then
				_should_aggresive_remove_artifacts="true"
			else
				failure "Because the automated-purge is considered unsafe, please follow the instructions above and re-run this script"
			fi
		else
			failure "Please re-run script after leftovers have manually been purged"
		fi
	}

	setup_report_confirm() {
		header "Nix config report"
		row "        Temp Dir" "$SCRATCH"
		row "        Nix Root" "$NIX_ROOT"
		row "     Build Users" "$NIX_USER_COUNT"
		row "  Build Group ID" "$NIX_BUILD_GROUP_ID"
		row "Build Group Name" "$NIX_BUILD_GROUP_NAME"
		if [ "${ALLOW_PREEXISTING_INSTALLATION:-}" != "" ]; then
			row "Preexisting Install" "Allowed"
		fi

		subheader "build users:"

		row "    Username" "UID"
		for i in $(seq 1 "$NIX_USER_COUNT"); do
			row "     $(nix_user_for_core "$i")" "$(nix_uid_for_core "$i")"
		done
		echo ""

		if ! ui_confirm "Ready to continue?"; then
			ok "Alright, no changes have been made :)"
			get_help
			trap finish_cleanup EXIT
			exit 1
		fi
	}

	create_build_group() {
		local primary_group_id

		task "Setting up the build group $NIX_BUILD_GROUP_NAME"
		if ! poly_group_exists "$NIX_BUILD_GROUP_NAME"; then
			poly_create_build_group
			row "            Created" "Yes"
		else
			primary_group_id=$(poly_group_id_get "$NIX_BUILD_GROUP_NAME")
			if [ "$primary_group_id" -ne "$NIX_BUILD_GROUP_ID" ]; then
				failure <<-EOF
				It seems the build group $NIX_BUILD_GROUP_NAME already exists, but
				with the UID $primary_group_id. This script can't really handle
				that right now, so I'm going to give up.

				You can fix this by editing this script and changing the
				NIX_BUILD_GROUP_ID variable near the top to from $NIX_BUILD_GROUP_ID
				to $primary_group_id and re-run.
				EOF
			else
				row "            Exists" "Yes"
			fi
		fi
	}

	create_build_user_for_core() {
		local coreid
		local username
		local uid

		coreid="$1"
		username=$(nix_user_for_core "$coreid")
		uid=$(nix_uid_for_core "$coreid")

		task "Setting up the build user $username"

		if ! poly_user_exists "$username"; then
			poly_create_build_user "$username" "$uid" "$coreid"
			row "           Created" "Yes"
		else
			actual_uid=$(poly_user_id_get "$username")
			if [ "$actual_uid" != "$uid" ]; then
				failure <<-EOF
				It seems the build user $username already exists, but with the UID
				with the UID '$actual_uid'. This script can't really handle that right
				now, so I'm going to give up.

				If you already created the users and you know they start from
				$actual_uid and go up from there, you can edit this script and change
				NIX_FIRST_BUILD_UID near the top of the file to $actual_uid and try
				again.
				EOF
			else
				row "            Exists" "Yes"
			fi
		fi

		if [ "$(poly_user_hidden_get "$username")" = "1" ]; then
			row "            Hidden" "Yes"
		else
			poly_user_hidden_set "$username"
			row "            Hidden" "Yes"
		fi

		if [ "$(poly_user_home_get "$username")" = "/var/empty" ]; then
			row "    Home Directory" "/var/empty"
		else
			poly_user_home_set "$username" "/var/empty"
			row "    Home Directory" "/var/empty"
		fi

		# We use grep instead of an equality check because it is difficult
		# to extract _just_ the user's note, instead it is prefixed with
		# some plist junk. This was causing the user note to always be set,
		# even if there was no reason for it.
		if poly_user_note_get "$username" | grep -q "Nix build user $coreid"; then
			row "              Note" "Nix build user $coreid"
		else
			poly_user_note_set "$username" "Nix build user $coreid"
			row "              Note" "Nix build user $coreid"
		fi

		if [ "$(poly_user_shell_get "$username")" = "/sbin/nologin" ]; then
			row "   Logins Disabled" "Yes"
		else
			poly_user_shell_set "$username" "/sbin/nologin"
			row "   Logins Disabled" "Yes"
		fi

		if poly_user_in_group_check "$username" "$NIX_BUILD_GROUP_NAME"; then
			row "  Member of $NIX_BUILD_GROUP_NAME" "Yes"
		else
			poly_user_in_group_set "$username" "$NIX_BUILD_GROUP_NAME"
			row "  Member of $NIX_BUILD_GROUP_NAME" "Yes"
		fi

		if [ "$(poly_user_primary_group_get "$username")" = "$NIX_BUILD_GROUP_ID" ]; then
			row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
		else
			poly_user_primary_group_set "$username" "$NIX_BUILD_GROUP_ID"
			row "    PrimaryGroupID" "$NIX_BUILD_GROUP_ID"
		fi
	}

	create_build_users() {
		for i in $(seq 1 "$NIX_USER_COUNT"); do
			create_build_user_for_core "$i"
		done
	}

	create_directories() {
		# FIXME: remove all of this because it duplicates LocalStore::LocalStore().
		task "Setting up the basic directory structure"
		if [ -d "$NIX_ROOT" ]; then
			# if /nix already exists, take ownership
			#
			# Caution: notes below are macOS-y
			# This is a bit of a goldilocks zone for taking ownership
			# if there are already files on the volume; the volume is
			# now mounted, but we haven't added a bunch of new files

			# this is probably a bit slow; I've been seeing 3.3-4s even
			# when promptly installed over a fresh single-user install.
			# In case anyone's aware of a shortcut.
			# `|| true`: .Trashes errors w/o full disk perm

			# rumor per #4488 that macOS 11.2 may not have
			# sbin on path, and that's where chown is, but
			# since this bit is cross-platform:
			# - first try with `command -vp` to try and find
			#   chown in the usual places
			#   * to work around some sort of deficiency in
			#     `command -p` in macOS bash 3.2, we also add
			#     PATH="$(getconf PATH 2>/dev/null)". As long as
			#     getconf is found, this should set a sane PATH
			#     which `command -p` in bash 3.2 appears to use.
			#     A bash with a properly-working `command -p`
			#     should ignore this hard-set PATH in favor of
			#     whatever it obtains internally. See
			#     github.com/NixOS/nix/issues/5768
			# - fall back on `command -v` which would find
			#   any chown on path
			# if we don't find one, the command is already
			# hiding behind || true, and the general state
			# should be one the user can repair once they
			# figure out where chown is...
			local get_chr_own="$(PATH="$(getconf PATH 2>/dev/null)" command -vp chown)"
			if [[ -z "$get_chr_own" ]]; then
				get_chr_own="$(command -v chown)"
			fi

			if [[ -z "$get_chr_own" ]]; then
				reminder <<-EOF
				I wanted to take root ownership of existing Nix store files,
				but I couldn't locate 'chown'. (You may need to fix your PATH.)
				To manually change file ownership, you can run:
					sudo chown -R 'root:$NIX_BUILD_GROUP_NAME' '$NIX_ROOT'
				EOF
			else
				_sudo "to take root ownership of existing Nix store files" \
								"$get_chr_own" -R "root:$NIX_BUILD_GROUP_NAME" "$NIX_ROOT" || true
			fi
		fi
		_sudo "to make the basic directory structure of Nix (part 1)" \
						install -dv -m 0755 /nix /nix/var /nix/var/log /nix/var/log/nix /nix/var/log/nix/drvs /nix/var/nix{,/db,/gcroots,/profiles,/temproots,/userpool,/daemon-socket} /nix/var/nix/{gcroots,profiles}/per-user

		_sudo "to make the basic directory structure of Nix (part 2)" \
						install -dv -g "$NIX_BUILD_GROUP_NAME" -m 1775 /nix/store

		_sudo "to place the default nix daemon configuration (part 1)" \
						install -dv -m 0555 /etc/nix
	}

	place_channel_configuration() {
		if [ -z "${NIX_INSTALLER_NO_CHANNEL_ADD:-}" ]; then
			echo "https://nixos.org/channels/nixpkgs-unstable nixpkgs" > "$SCRATCH/.nix-channels"
			_sudo "to set up the default system channel (part 1)" \
				install -m 0664 "$SCRATCH/.nix-channels" "$ROOT_HOME/.nix-channels"
		fi
	}

	install_from_extracted_nix() {
		task "Installing Nix"
		(
			cd "$EXTRACTED_NIX_PATH"

			_sudo "to copy the basic Nix files to the new store at $NIX_ROOT/store" \
					cp -RPp ./store/* "$NIX_ROOT/store/"

			_sudo "to make the new store non-writable at $NIX_ROOT/store" \
				chmod -R ugo-w "$NIX_ROOT/store/"

			if [ -d "$NIX_INSTALLED_NIX" ]; then
				echo "      Alright! We have our first nix at $NIX_INSTALLED_NIX"
			else
				failure <<-EOF
				Something went wrong, and I didn't find Nix installed at
				$NIX_INSTALLED_NIX.
				EOF
			fi

			_sudo "to load data for the first time in to the Nix Database" \
				HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-store" --load-db < ./.reginfo

			echo "      Just finished getting the nix database ready."
		)
	}

	cert_in_store() {
		# in a subshell
		# - change into the cert-file dir
		# - get the phyiscal pwd
		# and test if this path is in the Nix store
		[[ "$(cd -- "$(dirname "$NIX_SSL_CERT_FILE")" && exec pwd -P)" == "$NIX_ROOT/store/"* ]]
	}

	setup_default_profile() {
		task "Setting up the default profile"
		_sudo "to install a bootstrapping Nix in to the default profile" \
			HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-env" -i "$NIX_INSTALLED_NIX"

		if [ -z "${NIX_SSL_CERT_FILE:-}" ] || ! [ -f "${NIX_SSL_CERT_FILE:-}" ] || cert_in_store; then
			_sudo "to install a bootstrapping SSL certificate just for Nix in to the default profile" \
				HOME="$ROOT_HOME" "$NIX_INSTALLED_NIX/bin/nix-env" -i "$NIX_INSTALLED_CACERT"
			export NIX_SSL_CERT_FILE=/nix/var/nix/profiles/default/etc/ssl/certs/ca-bundle.crt
		fi

		if [ -z "${NIX_INSTALLER_NO_CHANNEL_ADD:-}" ]; then
			# Have to explicitly pass NIX_SSL_CERT_FILE as part of the sudo call,
			# otherwise it will be lost in environments where sudo doesn't pass
			# all the environment variables by default.
			if ! _sudo "to update the default channel in the default profile" \
				HOME="$ROOT_HOME" NIX_SSL_CERT_FILE="$NIX_SSL_CERT_FILE" "$NIX_INSTALLED_NIX/bin/nix-channel" --update nixpkgs; then
				reminder <<-EOF
				I had trouble fetching the nixpkgs channel (are you offline?)
				To try again later, run: sudo -i nix-channel --update nixpkgs
				EOF
			fi
		fi
	}

	place_nix_configuration() {
		cat <<-EOF > "$SCRATCH/nix.conf"
		$NIX_EXTRA_CONF
		build-users-group = $NIX_BUILD_GROUP_NAME
		EOF
		_sudo "to place the default nix daemon configuration (part 2)" \
			install -m 0664 "$SCRATCH/nix.conf" /etc/nix/nix.conf
	}


# 
# trigger the main function
# 
	# set an empty initial arg for bare invocations in case we need to
	# disambiguate someone directly invoking this later.
	if [ "${#@}" = 0 ]; then
		set ""
	fi

	# ACTION for override
	case "${1-}" in
		# uninstall)
		#     shift
		#     uninstall "$@";;
		# install == same as the no-arg condition for now (but, explicit)
		""|install)
			main;;
		*) # holding space for future options (like uninstall + install?)
			failure "install-multi-user: invalid argument";;
	esac
