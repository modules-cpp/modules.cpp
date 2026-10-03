# What an image should contain, read from the manifests. Sourced after
# lib/common.sh; defines functions and changes nothing when it is read.
#
# The build injects a platform provider for every platform interface an
# application's closure reaches, following the bindings of the selected board,
# its bases, and its SDK, to a fixed point. This file walks the same manifests
# the same way, independently of the build, so a script can say which provider
# objects an image must carry and which it must not, and check the build
# against that rather than against a list someone wrote down.
#
# Everything here reads only a manifest's front matter, the lines between its
# first two ---, which is where the manifest grammar puts every key.
#
# Every sort here relies on the sourcing script's LC_ALL=C. A UTF-8 locale's
# collation ignores punctuation, which orders platform.pico_epaper_b before
# platform.pico.mcu on one machine and after it on another.

# The value of every "key: value" line in a manifest's front matter, one a line.
mm_manifest_values() {
    awk -v key="$2" '
        NR == 1 && $0 == "---" { inside = 1; next }
        inside && $0 == "---" { exit }
        inside && index($0, key ":") == 1 {
            value = substr($0, length(key) + 2)
            sub(/^[ \t]+/, "", value)
            print value
        }
    ' "$1"
}

mm_manifest_value() {
    mm_manifest_values "$1" "$2" | head -1
}

# Whether a manifest declares a platform interface: a platform-interface key,
# which carries no value.
mm_is_interface() {
    awk 'NR == 1 && $0 == "---" { inside = 1; next }
         inside && $0 == "---" { exit }
         inside && /^platform-interface:/ { found = 1 }
         END { exit !found }' "$1"
}

# The manifest declaring a module, a board, an SDK, or a library of this name.
mm_module_manifest() {
    grep -rlx --include=mm.mdy "module: $1" modules platforms boards libraries 2>/dev/null |
        head -1
}

mm_named_manifest() {
    for manifest in $(grep -rlx --include=mm.mdy "name: $2" platforms boards libraries 2>/dev/null); do
        if [ "$(mm_manifest_value "$manifest" kind)" = "$1" ]; then
            echo "$manifest"
            return 0
        fi
    done
    return 1
}

# "interface provider" for every binding the lane selects, the nearest
# declaration winning: the board, then each base in turn, then the SDK. With
# no board, the SDK's alone. Printed nearest first, each interface once.
mm_bindings() {
    sdk_name=$1
    board_name=$2
    {
        depth=0
        name=$board_name
        while [ -n "$name" ]; do
            manifest=$(mm_named_manifest board "$name") || {
                echo "$test_name: no board manifest for $name" >&2
                exit 64
            }
            mm_manifest_values "$manifest" platform-provider | sed "s/^/$depth /"
            depth=$((depth + 1))
            base=$(mm_manifest_value "$manifest" derives-from)
            if [ -z "$base" ] && [ -z "$sdk_name" ]; then
                sdk_name=$(mm_manifest_value "$manifest" sdk)
            fi
            name=$base
        done
        sdk_manifest=$(mm_named_manifest sdk "$sdk_name") || {
            echo "$test_name: no SDK manifest for $sdk_name" >&2
            exit 64
        }
        mm_manifest_values "$sdk_manifest" platform-provider | sed "s/^/999 /"
    } | sort -s -n -k1,1 | awk '!seen[$2]++ { print $2, $3 }'
}

# Every provider module any binding in the lane names, overridden ones
# included: the set an image's provider objects are drawn from.
mm_bound_providers() {
    sdk_name=$1
    board_name=$2
    {
        name=$board_name
        while [ -n "$name" ]; do
            manifest=$(mm_named_manifest board "$name") || exit 64
            mm_manifest_values "$manifest" platform-provider | awk '{ print $2 }'
            base=$(mm_manifest_value "$manifest" derives-from)
            if [ -z "$base" ] && [ -z "$sdk_name" ]; then
                sdk_name=$(mm_manifest_value "$manifest" sdk)
            fi
            name=$base
        done
        mm_manifest_values "$(mm_named_manifest sdk "$sdk_name")" platform-provider |
            awk '{ print $2 }'
    } | sort -u
}

# The modules an application's closure contains once providers are injected:
# its own uses, theirs, and for every platform interface reached, the bound
# provider and its uses, to a fixed point. bindings is the output of
# mm_bindings. One module a line, sorted.
mm_closure() {
    app_manifest=$1
    bindings=$2
    pending=$(mm_manifest_values "$app_manifest" use)
    seen=" "
    while [ -n "$pending" ]; do
        next=
        for module in $pending; do
            case "$seen" in *" $module "*) continue ;; esac
            seen="$seen$module "
            manifest=$(mm_module_manifest "$module")
            [ -n "$manifest" ] || continue
            next="$next $(mm_manifest_values "$manifest" use | tr '\n' ' ')"
            if mm_is_interface "$manifest"; then
                provider=$(printf '%s\n' "$bindings" | awk -v iface="$module" '$1 == iface { print $2 }')
                [ -n "$provider" ] && next="$next $provider"
            fi
        done
        pending=$next
    done
    printf '%s\n' $seen | sort -u
}

# The platform interfaces in a closure the lane binds no provider for. The
# build makes such an application unavailable on the lane, before compiling
# anything, and so does every script that asks.
mm_unbound() {
    for module in $1; do
        manifest=$(mm_module_manifest "$module")
        [ -n "$manifest" ] || continue
        mm_is_interface "$manifest" || continue
        printf '%s\n' "$2" | awk -v iface="$module" '$1 == iface { found = 1 } END { exit found }' &&
            echo "$module"
    done
}

# The link inputs of every library a module in the closure names.
mm_closure_link_inputs() {
    for module in $1; do
        manifest=$(mm_module_manifest "$module")
        [ -n "$manifest" ] || continue
        for library in $(mm_manifest_values "$manifest" library); do
            library_manifest=$(mm_named_manifest library "$library") || continue
            mm_manifest_values "$library_manifest" link-input
        done
    done | sort -u
}

# The expectations for one image, as "provider NAME COUNT" lines for every
# bound provider and every platform.* module the closure reaches, "driver
# NAMESPACE" for every non-interface, non-platform module a reached provider
# uses, and "library NAME" for every link input. The drivers are what catch a
# board wired to an interface but not to a controller, and their namespaces
# follow the module names: mm.lcd.st7789 is mm::lcd::st7789.
mm_expectations() {
    closure=$1
    bound=$2
    reached=
    for module in $(printf '%s\n' $closure | grep '^platform\.'); do
        mm_is_interface "$(mm_module_manifest "$module")" || reached="$reached $module"
    done
    for provider in $(printf '%s\n' $bound $reached | sort -u); do
        case " $(printf '%s ' $closure)" in
            *" $provider "*) echo "provider $provider 1" ;;
            *) echo "provider $provider 0" ;;
        esac
    done
    for provider in $reached; do
        manifest=$(mm_module_manifest "$provider")
        for module in $(mm_manifest_values "$manifest" use); do
            case "$module" in platform.*) continue ;; esac
            module_manifest=$(mm_module_manifest "$module")
            [ -n "$module_manifest" ] || continue
            if ! mm_is_interface "$module_manifest"; then
                echo "driver $(printf '%s\n' "$module" | sed 's/\./::/g')"
            fi
        done
    done | sort -u
    for input in $(mm_closure_link_inputs "$closure"); do
        echo "library $input"
    done
}

# pico or linux for a board: the Linux family names and sdk are Linux, and any
# other board is whatever its chain's SDK is.
mm_board_platform() {
    case "$1" in
        generic|sdl|epaper|lcd|ili9341|sdk) echo linux; return 0 ;;
    esac
    name=$1
    while [ -n "$name" ]; do
        manifest=$(mm_named_manifest board "$name") || return 1
        sdk_name=$(mm_manifest_value "$manifest" sdk)
        [ -n "$sdk_name" ] && break
        name=$(mm_manifest_value "$manifest" derives-from)
    done
    case "$sdk_name" in
        pico-*) echo pico ;;
        linux-*) echo linux ;;
        *) return 1 ;;
    esac
}
