#!/usr/bin/env bash
################################################################################
# QUIC Cross-Implementation Interoperability Matrix Test
#
# Tests quicX against other popular QUIC implementations to generate
# a comprehensive compatibility matrix similar to https://interop.seemann.io/
#
# Usage:
#   ./cross_impl_matrix.sh [options]
#
# Options:
#   --implementations <list>  Comma-separated list of implementations to test
#   --output <file>           Output file for results (default: matrix_results.md)
#   --format <type>           Output format: markdown|html|json (default: markdown)
#
################################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

################################################################################
# Configuration
################################################################################

# Helper to get image for implementation
get_impl_image() {
    case "$1" in
        quicx) echo "quicx-interop:latest" ;;
        quiche) echo "cloudflare/quiche:latest" ;;
        ngtcp2) echo "ngtcp2/ngtcp2:latest" ;;
        quic-go) echo "martenseemann/quic-go-interop:latest" ;;
        mvfst) echo "mvfst/mvfst-qns:latest" ;;
        quinn) echo "quinn-rs/quinn-interop:latest" ;;
        aioquic) echo "aiortc/aioquic:latest" ;;
        picoquic) echo "private-octopus/picoquic:latest" ;;
        neqo) echo "mozilla/neqo-qns:latest" ;;
        lsquic) echo "litespeedtech/lsquic-qir:latest" ;;
        *) echo "" ;;
    esac
}

# Test scenarios to run
TEST_SCENARIOS=(
    "handshake"
    "transfer"
    "retry"
    "resumption"
    "zerortt"
    "http3"
    "chacha20"
)

# Selected implementations to test
SELECTED_IMPLS=()
OUTPUT_FILE="matrix_results.md"
OUTPUT_FORMAT="markdown"

# Temporary file for results
RESULTS_FILE="/tmp/quicx_matrix_results_$$"
rm -f "$RESULTS_FILE"

################################################################################
# Helper Functions
################################################################################

log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

check_docker_image() {
    local impl=$1
    local image=$(get_impl_image "$impl")

    if [ -z "$image" ]; then
        return 1
    fi

    # Try to pull image if not available locally
    if ! docker images | grep -q "${image%%:*}"; then
        log_info "Pulling Docker image for $impl..."
        docker pull "$image" 2>/dev/null || return 1
    fi

    return 0
}

run_cross_test() {
    local server_impl=$1
    local client_impl=$2
    local scenario=$3

    log_info "Testing: $client_impl (client) -> $server_impl (server) [$scenario]"

    # Start server
    local server_port=$((4433 + RANDOM % 1000))
    local server_container="${server_impl}-server-$$"
    local server_image=$(get_impl_image "$server_impl")

    docker run -d --rm \
        --name "$server_container" \
        --network host \
        -e ROLE=server \
        -e PORT=$server_port \
        -e TESTCASE=$scenario \
        "$server_image" \
        > /dev/null 2>&1 || return 1

    sleep 3

    # Run client
    local client_container="${client_impl}-client-$$"
    local client_image=$(get_impl_image "$client_impl")
    local result=0

    docker run --rm \
        --name "$client_container" \
        --network host \
        -e ROLE=client \
        -e SERVER=localhost \
        -e PORT=$server_port \
        -e TESTCASE=$scenario \
        "$client_image" \
        > /dev/null 2>&1 || result=1

    # Cleanup server
    docker stop "$server_container" 2>/dev/null || true

    return $result
}

save_result() {
    local scenario=$1
    local server=$2
    local client=$3
    local result=$4
    echo "${scenario}:${server}:${client}:${result}" >> "$RESULTS_FILE"
}

get_result() {
    local scenario=$1
    local server=$2
    local client=$3
    local res=$(grep "^${scenario}:${server}:${client}:" "$RESULTS_FILE" | cut -d: -f4)
    echo "${res:-•}"
}

################################################################################
# Matrix Generation
################################################################################

run_full_matrix() {
    log_info "Running full compatibility matrix..."

    local total_tests=0
    local completed_tests=0

    # Calculate total tests
    for scenario in "${TEST_SCENARIOS[@]}"; do
        for server in "${SELECTED_IMPLS[@]}"; do
            for client in "${SELECTED_IMPLS[@]}"; do
                ((total_tests++))
            done
        done
    done

    log_info "Total tests to run: $total_tests"

    # Run tests
    for scenario in "${TEST_SCENARIOS[@]}"; do
        log_info "Testing scenario: $scenario"

        for server in "${SELECTED_IMPLS[@]}"; do
            for client in "${SELECTED_IMPLS[@]}"; do
                ((completed_tests++))

                if [ "$server" = "$client" ]; then
                    # Self-test
                    save_result "$scenario" "$server" "$client" "SELF"
                else
                    # Cross-test
                    if run_cross_test "$server" "$client" "$scenario"; then
                        save_result "$scenario" "$server" "$client" "✓"
                    else
                        save_result "$scenario" "$server" "$client" "✗"
                    fi
                fi

                echo -n "."
            done
        done
        echo ""
    done

    log_info "Completed $completed_tests/$total_tests tests"
}

################################################################################
# Output Formatting
################################################################################

generate_markdown_matrix() {
    cat > "$OUTPUT_FILE" << EOF
# QUIC Implementation Interoperability Matrix

**Test Date:** $(date '+%Y-%m-%d %H:%M:%S')

Generated by quicX interop test suite.

## Legend

- ✓ = Test passed
- ✗ = Test failed
- • = Not tested
- SELF = Self-test (same implementation as client and server)

EOF

    for scenario in "${TEST_SCENARIOS[@]}"; do
        cat >> "$OUTPUT_FILE" << EOF

## Test Scenario: $scenario

| Server -> Client | $(printf '%s | ' "${SELECTED_IMPLS[@]}") |
|---|$(for i in "${SELECTED_IMPLS[@]}"; do echo -n "---|"; done) |
EOF

        for server in "${SELECTED_IMPLS[@]}"; do
            echo -n "| **$server** |" >> "$OUTPUT_FILE"

            for client in "${SELECTED_IMPLS[@]}"; do
                local result=$(get_result "$scenario" "$server" "$client")
                echo -n " $result |" >> "$OUTPUT_FILE"
            done

            echo "" >> "$OUTPUT_FILE"
        done
    done

    cat >> "$OUTPUT_FILE" << EOF

## Summary

This matrix shows the interoperability test results between different QUIC implementations.

- **Rows** represent server implementations
- **Columns** represent client implementations
- **Cells** show the test result for that server-client combination

### Test Scenarios

1. **handshake** - Basic QUIC handshake
2. **transfer** - Data transfer and flow control
3. **retry** - Retry packet handling
4. **resumption** - Session resumption (1-RTT)
5. **zerortt** - 0-RTT early data
6. **http3** - HTTP/3 functionality
7. **chacha20** - ChaCha20 cipher suite

### Implementations Tested

EOF

    for impl in "${SELECTED_IMPLS[@]}"; do
        echo "- **$impl**: $(get_impl_image "$impl")" >> "$OUTPUT_FILE"
    done

    cat >> "$OUTPUT_FILE" << EOF

---

*Generated by quicX QUIC Interoperability Test Suite*
*Based on [QUIC Interop Runner](https://github.com/quic-interop/quic-interop-runner)*

EOF

    log_info "Markdown matrix saved to: $OUTPUT_FILE"
}

generate_json_output() {
    local json_file="${OUTPUT_FILE%.md}.json"

    cat > "$json_file" << EOF
{
  "metadata": {
    "generated_at": "$(date -u '+%Y-%m-%dT%H:%M:%SZ')",
    "test_suite": "quicX",
    "implementations": [$(printf '"%s",' "${SELECTED_IMPLS[@]}" | sed 's/,$//')],
    "scenarios": [$(printf '"%s",' "${TEST_SCENARIOS[@]}" | sed 's/,$//')],
    "total_tests": $((${#SELECTED_IMPLS[@]} * ${#SELECTED_IMPLS[@]} * ${#TEST_SCENARIOS[@]}))
  },
  "results": [
EOF

    local first=true
    for scenario in "${TEST_SCENARIOS[@]}"; do
        for server in "${SELECTED_IMPLS[@]}"; do
            for client in "${SELECTED_IMPLS[@]}"; do
                local result=$(get_result "$scenario" "$server" "$client")
                if [ "$result" = "•" ]; then result="unknown"; fi

                if [ "$first" = false ]; then
                    echo "," >> "$json_file"
                fi
                first=false

                cat >> "$json_file" << EOF
    {
      "scenario": "$scenario",
      "server": "$server",
      "client": "$client",
      "result": "$result"
    }
EOF
            done
        done
    done

    cat >> "$json_file" << EOF
  ]
}
EOF

    log_info "JSON output saved to: $json_file"
}

generate_html_report() {
    local html_file="${OUTPUT_FILE%.md}.html"

    cat > "$html_file" << EOF
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>QUIC Interoperability Matrix - quicX</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
            max-width: 1400px;
            margin: 40px auto;
            padding: 20px;
            background: #f5f5f5;
        }
        h1 {
            color: #333;
            border-bottom: 3px solid #4CAF50;
            padding-bottom: 10px;
        }
        h2 {
            color: #555;
            margin-top: 30px;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            background: white;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            margin: 20px 0;
        }
        th, td {
            padding: 12px;
            text-align: center;
            border: 1px solid #ddd;
        }
        th {
            background: #4CAF50;
            color: white;
            font-weight: bold;
        }
        tr:nth-child(even) {
            background: #f9f9f9;
        }
        tr:hover {
            background: #f0f0f0;
        }
        .pass { color: #4CAF50; font-size: 20px; }
        .fail { color: #f44336; font-size: 20px; }
        .self { color: #999; font-style: italic; }
        .legend {
            background: white;
            padding: 15px;
            border-radius: 5px;
            margin: 20px 0;
        }
        .legend span {
            margin-right: 20px;
        }
        .metadata {
            color: #666;
            font-size: 14px;
            margin-bottom: 20px;
        }
    </style>
</head>
<body>
    <h1>🚀 QUIC Implementation Interoperability Matrix</h1>

    <div class="metadata">
        <strong>Test Date:</strong> $(date '+%Y-%m-%d %H:%M:%S')<br>
        <strong>Test Suite:</strong> quicX QUIC Interoperability Test<br>
        <strong>Implementations Tested:</strong> ${#SELECTED_IMPLS[@]}<br>
        <strong>Scenarios Tested:</strong> ${#TEST_SCENARIOS[@]}
    </div>

    <div class="legend">
        <strong>Legend:</strong>
        <span class="pass">✓ Pass</span>
        <span class="fail">✗ Fail</span>
        <span class="self">SELF Self-test</span>
        <span>• Not tested</span>
    </div>
EOF

    for scenario in "${TEST_SCENARIOS[@]}"; do
        cat >> "$html_file" << EOF

    <h2>Test Scenario: $scenario</h2>
    <table>
        <thead>
            <tr>
                <th>Server -> Client</th>
EOF

        for client in "${SELECTED_IMPLS[@]}"; do
            echo "                <th>$client</th>" >> "$html_file"
        done

        echo "            </tr>" >> "$html_file"
        echo "        </thead>" >> "$html_file"
        echo "        <tbody>" >> "$html_file"

        for server in "${SELECTED_IMPLS[@]}"; do
            echo "            <tr>" >> "$html_file"
            echo "                <th>$server</th>" >> "$html_file"

            for client in "${SELECTED_IMPLS[@]}"; do
                local result=$(get_result "$scenario" "$server" "$client")

                local class=""
                if [ "$result" = "✓" ]; then
                    class="pass"
                elif [ "$result" = "✗" ]; then
                    class="fail"
                elif [ "$result" = "SELF" ]; then
                    class="self"
                fi

                echo "                <td class=\"$class\">$result</td>" >> "$html_file"
            done

            echo "            </tr>" >> "$html_file"
        done

        echo "        </tbody>" >> "$html_file"
        echo "    </table>" >> "$html_file"
    done

    cat >> "$html_file" << EOF

    <div style="margin-top: 50px; padding-top: 20px; border-top: 1px solid #ddd; color: #999; font-size: 12px;">
        Generated by <strong>quicX QUIC Interoperability Test Suite</strong><br>
        Based on <a href="https://github.com/quic-interop/quic-interop-runner" target="_blank">QUIC Interop Runner</a>
    </div>
</body>
</html>
EOF

    log_info "HTML report saved to: $html_file"
}

################################################################################
# Main
################################################################################

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            --implementations)
                IFS=',' read -ra SELECTED_IMPLS <<< "$2"
                shift 2
                ;;
            --output)
                OUTPUT_FILE="$2"
                shift 2
                ;;
            --format)
                OUTPUT_FORMAT="$2"
                shift 2
                ;;
            -h|--help)
                echo "Usage: $0 [options]"
                echo ""
                echo "Options:"
                echo "  --implementations <list>  Comma-separated list (default: quicx,quiche,ngtcp2)"
                echo "  --output <file>           Output file (default: matrix_results.md)"
                echo "  --format <type>           Format: markdown|html|json (default: markdown)"
                echo ""
                echo "Available implementations:"
                echo "  - quicx"
                echo "  - quiche"
                echo "  - ngtcp2"
                echo "  - quic-go"
                echo "  - mvfst"
                echo "  - quinn"
                echo "  - aioquic"
                echo "  - picoquic"
                echo "  - neqo"
                echo "  - lsquic"
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                exit 1
                ;;
        esac
    done

    # Default implementations if none specified
    if [ ${#SELECTED_IMPLS[@]} -eq 0 ]; then
        SELECTED_IMPLS=(quicx quiche ngtcp2)
    fi
}

main() {
    parse_arguments "$@"

    echo "========================================="
    echo "QUIC Cross-Implementation Matrix Test"
    echo "========================================="
    echo ""

    log_info "Implementations to test: ${SELECTED_IMPLS[*]}"
    log_info "Output file: $OUTPUT_FILE"
    log_info "Output format: $OUTPUT_FORMAT"
    echo ""

    # Verify Docker images
    local valid_impls=()
    for impl in "${SELECTED_IMPLS[@]}"; do
        if ! check_docker_image "$impl"; then
            log_error "Docker image not available for: $impl"
            log_info "Skipping $impl"
        else
            valid_impls+=("$impl")
        fi
    done
    SELECTED_IMPLS=("${valid_impls[@]}")

    if [ ${#SELECTED_IMPLS[@]} -lt 2 ]; then
        log_error "Need at least 2 implementations to test"
        exit 1
    fi

    # Run matrix tests
    run_full_matrix

    # Generate output
    case $OUTPUT_FORMAT in
        markdown|md)
            generate_markdown_matrix
            ;;
        html)
            generate_html_report
            ;;
        json)
            generate_json_output
            ;;
        all)
            generate_markdown_matrix
            generate_html_report
            generate_json_output
            ;;
        *)
            log_error "Unknown output format: $OUTPUT_FORMAT"
            exit 1
            ;;
    esac

    # Cleanup
    rm -f "$RESULTS_FILE"

    echo ""
    log_info "Matrix test complete!"
}

main "$@"
