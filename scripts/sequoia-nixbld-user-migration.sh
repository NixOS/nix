#!/usr/bin/env bash

set -eo pipefail

((NEW_NIX_FIRST_BUILD_UID=351))
((TEMP_NIX_FIRST_BUILD_UID=31000))

nix_user_n() {
	printf "_nixbld%d" "$1"
}

id_unavailable(){
	dscl . list /Users UniqueID | grep -E '\b'"$1"'\b' >/dev/null
}

any_nixbld(){
	dscl . list /Users UniqueID | grep -E '\b_nixbld' >/dev/null
}

dsclattr() {
	dscl . -read "$1" | awk "/$2/ { print \$2 }"
}

re_create_nixbld_user(){
	local name uid

	name="$1"
	uid="$2"
	gid="$3"

	sudo /usr/bin/dscl . -create "/Users/$name" "UniqueID" "$uid"
	sudo /usr/bin/dscl . -create "/Users/$name" "IsHidden" "1"
	sudo /usr/bin/dscl . -create "/Users/$name" "NFSHomeDirectory" "/var/empty"
	sudo /usr/bin/dscl . -create "/Users/$name" "RealName" "Nix build user $name"
	sudo /usr/bin/dscl . -create "/Users/$name" "UserShell" "/sbin/nologin"
	sudo /usr/bin/dscl . -create "/Users/$name" "PrimaryGroupID" "$gid"
}

hit_id_cap(){
	echo "We've hit UID 400 without placing all of your users :("
	echo "You should use the commands in this script as a starting"
	echo "point to review your UID-space and manually move the"
	echo "remaining users (or delete them, if you don't need them)."
}

# evacuate the role-uid space to simplify final placement logic
temporarily_move_existing_nixbld_uids(){
	local name uid next_id user_n

	((next_id=TEMP_NIX_FIRST_BUILD_UID))

	echo ""
	echo "Step 1: move existing _nixbld users out of the destination UID range."

	while read -r name uid; do
		# iterate for a clean ID
		while id_unavailable "$next_id"; do
			((next_id++))
			# We really want to get these all placed, but I guess there's
			# some risk we iterate forever--so we'll give up after 9k uids.
			if ((next_id >= 40000)); then
				echo "We've hit UID 40000 without temporarily placing all of your users :("
				echo "You should use the commands in this script as a starting"
				echo "point to review your UID-space and manually move the"
				echo "remaining users to any open UID over 1000."
				exit 1
			fi
		done
		sudo dscl . -create "/Users/$name" UniqueID "$next_id"
		echo "   Temporarily moved $name from uid $uid -> $next_id"

	done < <(dscl . list /Users UniqueID | grep _nixbld | sort -n -k2)
}

change_nixbld_uids(){
	local existing_gid name next_id user_n

	((next_id=NEW_NIX_FIRST_BUILD_UID))
	((user_n=1))
	name="$(nix_user_n "$user_n")"
	existing_gid="$(dsclattr "/Groups/nixbld" "PrimaryGroupID")"

	# we know that we have *some* nixbld users, but macOS may have
	# already clobbered the first few users if this system has been
	# upgraded

	echo ""
	echo "Step 2: re-create missing early _nixbld# users."

	until dscl . read "/Users/$name" &>/dev/null; do
		# iterate for a clean ID
		while id_unavailable "$next_id"; do
			((next_id++))
			if ((next_id >= 400)); then
				hit_id_cap
				exit 1
			fi
		done

		re_create_nixbld_user "$name" "$next_id" "$existing_gid"
		echo "      $name was missing; created with uid: $next_id"

		((user_n++))
		name="$(nix_user_n "$user_n")"
	done

	echo ""
	echo "Step 3: relocate remaining _nixbld# UIDs to $next_id+"

	# start at first _nixbld# not re-created above and increment
	# until _nixbld<n> doesn't exist
	while dscl . read "/Users/$name" &>/dev/null; do
		# iterate for a clean ID
		while id_unavailable "$next_id"; do
			((next_id++))
			if ((next_id >= 400)); then
				hit_id_cap
				exit 1
			fi
		done

		sudo dscl . -create "/Users/$name" UniqueID "$next_id"
		echo "      $name migrated to uid: $next_id"

		((user_n++))
		name="$(nix_user_n "$user_n")"
	done

	if ((user_n == 1)); then
		echo "Didn't find _nixbld1. Perhaps you have single-user Nix?"
		exit 1
	else
		echo "Migrated $((user_n - 1)) users. If you want to double-check, try:"
		echo "dscl . list /Users UniqueID | grep _nixbld | sort -n -k2"
	fi
}
needs_migration(){
	local name uid next_id user_n

	((next_id=NEW_NIX_FIRST_BUILD_UID))
	((user_n=1))

	while read -r name uid; do
		expected_name="$(nix_user_n "$user_n")"
		if [[ "$expected_name" != "$name" ]]; then
			return 0
		fi
		if [[ "$next_id" != "$uid" ]]; then
			return 0
		fi
		((next_id++))
		((user_n++))
	done < <(dscl . list /Users UniqueID | grep _nixbld | sort -n -k2)
	return 1
}


if any_nixbld; then
	if needs_migration; then
		echo "Attempting to migrate _nixbld users."
		temporarily_move_existing_nixbld_uids
		change_nixbld_uids
	else
		echo "_nixbld users already appear to be migrated."
	fi
else
	echo "Didn't find any _nixbld users. Perhaps you have single-user Nix?"
	exit 1
fi
