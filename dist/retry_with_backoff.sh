#!/bin/bash
# Retry helper with exponential backoff for network operations
# Usage: retry_with_backoff <command> [args...]
#
# Retries the command up to 3 times with exponential backoff:
# - Attempt 1: immediate
# - Attempt 2: after 30 seconds
# - Attempt 3: after 2 minutes (120 seconds)
# - Attempt 4: after 8 minutes (480 seconds)
# 
# Returns the exit code of the final attempt.

retry_with_backoff() {
    local -a cmd=("$@")
    local attempt=1
    local max_attempts=4
    local -a delays=(0 30 120 480)  # seconds: immediate, 30s, 2min, 8min
    
    while true; do
        echo "Attempt $attempt/$max_attempts: ${cmd[@]}"
        
        if "${cmd[@]}"; then
            echo "✓ Command succeeded on attempt $attempt"
            return 0
        fi
        
        local exit_code=$?
        
        if [[ $attempt -eq $max_attempts ]]; then
            echo "✗ Command failed after $max_attempts attempts"
            return $exit_code
        fi
        
        local delay=${delays[$attempt]}
        echo "✗ Attempt $attempt failed with exit code $exit_code"
        
        if [[ $delay -gt 0 ]]; then
            echo "Waiting ${delay}s before retry..."
            sleep "$delay"
        fi
        
        ((attempt++))
    done
}

# Export the function so it can be used in subshells
export -f retry_with_backoff
