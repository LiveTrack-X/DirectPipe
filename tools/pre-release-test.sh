#!/bin/bash
# DirectPipe Pre-Release Test Suite / 릴리스 전 테스트 스위트
# Usage: bash tools/pre-release-test.sh [--skip-build] [--skip-api] [--api-only] [--version-only]
set -eo pipefail

# ─── Colors ───
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'
pass() { echo -e "  ${GREEN}PASS${NC} $1"; }
fail() { echo -e "  ${RED}FAIL${NC} $1"; }
skip() { echo -e "  ${YELLOW}SKIP${NC} $1"; }
info() { echo -e "  ${CYAN}INFO${NC} $1"; }

# ─── Args ───
SKIP_BUILD=false; SKIP_API=false; API_ONLY=false; VERSION_ONLY=false
for arg in "$@"; do
  case $arg in
    --skip-build)  SKIP_BUILD=true ;;
    --skip-api)    SKIP_API=true ;;
    --api-only)    API_ONLY=true ;;
    --version-only) VERSION_ONLY=true ;;
  esac
done

# ─── Paths ───
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
BUILD_DIR="$PROJECT_ROOT/build"

cd "$PROJECT_ROOT"

# ─── Results tracking ───
RESULTS=()
add_result() { RESULTS+=("$1"); }

echo ""
echo "============================================"
echo "  DirectPipe Pre-Release Test Suite"
echo "============================================"
echo ""

# ═══════════════════════════════════════════════
# Step 1: Version Consistency Check
# ═══════════════════════════════════════════════
echo "[Step 1] Version Consistency Check"
echo "─────────────────────────────────────"

# Extract canonical version from root CMakeLists.txt
CANONICAL=$(sed -n 's/.*project(DirectPipe VERSION \([0-9]*\.[0-9]*\.[0-9]*\).*/\1/p' CMakeLists.txt 2>/dev/null || echo "")
if [[ -z "$CANONICAL" ]]; then
  fail "Could not extract version from CMakeLists.txt"
  add_result "Version Check|FAIL:no version in CMakeLists.txt"
else
  info "Canonical version: $CANONICAL"
  VER_FAIL=0

  # host/CMakeLists.txt
  if grep -q "VERSION \"$CANONICAL\"" host/CMakeLists.txt 2>/dev/null; then
    pass "host/CMakeLists.txt"
  else
    fail "host/CMakeLists.txt"; VER_FAIL=$((VER_FAIL+1))
  fi

  # Stream Deck manifest.json
  if grep -q "\"Version\": \"${CANONICAL}.0\"" com.directpipe.directpipe.sdPlugin/manifest.json 2>/dev/null; then
    pass "SD manifest.json"
  else
    fail "SD manifest.json (expected ${CANONICAL}.0)"; VER_FAIL=$((VER_FAIL+1))
  fi

  # Stream Deck package.json
  if grep -q "\"version\": \"$CANONICAL\"" com.directpipe.directpipe.sdPlugin/package.json 2>/dev/null; then
    pass "SD package.json"
  else
    fail "SD package.json"; VER_FAIL=$((VER_FAIL+1))
  fi

  # README.md badge (optional — may not have version badge)
  if [[ -f README.md ]]; then
    if grep -q "version-[0-9]" README.md 2>/dev/null; then
      if grep -q "version-$CANONICAL" README.md 2>/dev/null; then
        pass "README.md badge"
      else
        fail "README.md badge"; VER_FAIL=$((VER_FAIL+1))
      fi
    else
      skip "README.md (no version badge found)"
    fi
  fi

  # docs/USER_GUIDE.md
  if [[ -f docs/USER_GUIDE.md ]]; then
    if grep -q "Version [0-9]" docs/USER_GUIDE.md 2>/dev/null; then
      if grep -q "Version $CANONICAL" docs/USER_GUIDE.md 2>/dev/null; then
        pass "docs/USER_GUIDE.md"
      else
        fail "docs/USER_GUIDE.md"; VER_FAIL=$((VER_FAIL+1))
      fi
    else
      skip "docs/USER_GUIDE.md (no version string found)"
    fi
  fi

  # docs/STREAMDECK_GUIDE.md
  if [[ -f docs/STREAMDECK_GUIDE.md ]]; then
    if grep -q "[0-9]\.[0-9]\.[0-9]\.[0-9]" docs/STREAMDECK_GUIDE.md 2>/dev/null; then
      if grep -q "${CANONICAL}.0" docs/STREAMDECK_GUIDE.md 2>/dev/null; then
        pass "docs/STREAMDECK_GUIDE.md"
      else
        fail "docs/STREAMDECK_GUIDE.md (expected ${CANONICAL}.0)"; VER_FAIL=$((VER_FAIL+1))
      fi
    else
      skip "docs/STREAMDECK_GUIDE.md (no version string found)"
    fi
  fi

  if [[ $VER_FAIL -eq 0 ]]; then
    add_result "Version Check|PASS"
  else
    add_result "Version Check|FAIL:$VER_FAIL mismatches"
  fi
fi
echo ""

if $VERSION_ONLY; then
  echo "Done (--version-only)"
  exit 0
fi

if $API_ONLY; then
  # Jump to API tests
  echo "[Step 5] API Integration Tests"
  echo "─────────────────────────────────────"
  if curl -s --connect-timeout 2 http://127.0.0.1:8766/api/status > /dev/null 2>&1; then
    info "DirectPipe detected on port 8766"
    if command -v node &> /dev/null && [[ -f test_api.js ]]; then
      if node test_api.js; then
        pass "API tests completed"
        add_result "API Tests|PASS"
      else
        fail "API tests had failures"
        add_result "API Tests|FAIL"
      fi
    else
      skip "node or test_api.js not found"
      add_result "API Tests|SKIP"
    fi
  else
    skip "DirectPipe not running on port 8766"
    add_result "API Tests|SKIP"
  fi
  echo ""
  # Print summary and exit
  echo "============================================"
  echo "  Summary"
  echo "============================================"
  OVERALL="PASS"
  for r in "${RESULTS[@]}"; do
    name="${r%%|*}"; status="${r#*|}"
    if [[ "$status" == PASS ]]; then
      echo -e "  ${GREEN}PASS${NC}  $name"
    elif [[ "$status" == SKIP ]]; then
      echo -e "  ${YELLOW}SKIP${NC}  $name"
    else
      echo -e "  ${RED}FAIL${NC}  $name (${status#FAIL:})"
      OVERALL="FAIL"
    fi
  done
  echo "============================================"
  if [[ "$OVERALL" == "PASS" ]]; then
    echo -e "  Overall: ${GREEN}PASS${NC}"
    exit 0
  else
    echo -e "  Overall: ${RED}FAIL${NC}"
    exit 1
  fi
fi

# ═══════════════════════════════════════════════
# Step 2: Build
# ═══════════════════════════════════════════════
if $SKIP_BUILD; then
  skip "Build (--skip-build)"
  add_result "Build|SKIP"
else
  echo "[Step 2] Release Build"
  echo "─────────────────────────────────────"
  # Kill running process
  taskkill //F //IM DirectPipe.exe 2>/dev/null && info "Killed running DirectPipe.exe" || true

  # Delete stale .rc files
  rm -f "$BUILD_DIR/host/DirectPipe_artefacts/JuceLibraryCode/DirectPipe_resources.rc" 2>/dev/null
  rm -f "$BUILD_DIR/plugins/receiver/DirectPipeReceiver_artefacts/JuceLibraryCode/DirectPipeReceiver_resources.rc" 2>/dev/null

  BUILD_START=$SECONDS
  if "$CMAKE" --build "$BUILD_DIR" --config Release --target DirectPipe DirectPipeReceiver_VST directpipe-tests directpipe-host-tests 2>&1 | tail -5; then
    BUILD_TIME=$((SECONDS - BUILD_START))
    pass "Build completed (${BUILD_TIME}s)"
    add_result "Build|PASS"
  else
    fail "Build failed"
    add_result "Build|FAIL"
  fi
  echo ""
fi

# ═══════════════════════════════════════════════
# Step 3: Core Unit Tests
# ═══════════════════════════════════════════════
echo "[Step 3] Core Unit Tests"
echo "─────────────────────────────────────"
CORE_TEST_EXE="$BUILD_DIR/tests/Release/directpipe-tests.exe"
if [[ -f "$CORE_TEST_EXE" ]]; then
  if "$CORE_TEST_EXE" 2>&1 | tail -3; then
    pass "Core tests"
    add_result "Core Tests|PASS"
  else
    fail "Core tests"
    add_result "Core Tests|FAIL"
  fi
else
  skip "Core test exe not found ($CORE_TEST_EXE)"
  add_result "Core Tests|SKIP"
fi
echo ""

# ═══════════════════════════════════════════════
# Step 4: Host Unit Tests
# ═══════════════════════════════════════════════
echo "[Step 4] Host Unit Tests"
echo "─────────────────────────────────────"
HOST_TEST_EXE="$BUILD_DIR/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe"
if [[ -f "$HOST_TEST_EXE" ]]; then
  if "$HOST_TEST_EXE" 2>&1 | tail -3; then
    pass "Host tests"
    add_result "Host Tests|PASS"
  else
    fail "Host tests"
    add_result "Host Tests|FAIL"
  fi
else
  skip "Host test exe not found ($HOST_TEST_EXE)"
  add_result "Host Tests|SKIP"
fi
echo ""

# ═══════════════════════════════════════════════
# Step 5: API Integration Tests
# ═══════════════════════════════════════════════
if $SKIP_API; then
  skip "API Tests (--skip-api)"
  add_result "API Tests|SKIP"
else
  echo "[Step 5] API Integration Tests"
  echo "─────────────────────────────────────"
  if curl -s --connect-timeout 2 http://127.0.0.1:8766/api/status > /dev/null 2>&1; then
    info "DirectPipe detected on port 8766"
    if command -v node &> /dev/null && [[ -f test_api.js ]]; then
      if node test_api.js; then
        pass "API tests"
        add_result "API Tests|PASS"
      else
        fail "API tests"
        add_result "API Tests|FAIL"
      fi
    else
      skip "node or test_api.js not found"
      add_result "API Tests|SKIP"
    fi
  else
    skip "DirectPipe not running on port 8766"
    add_result "API Tests|SKIP"
  fi
  echo ""
fi

# ═══════════════════════════════════════════════
# Step 6: Git Status
# ═══════════════════════════════════════════════
echo "[Step 6] Git Status"
echo "─────────────────────────────────────"
DIRTY_COUNT=$(git status --porcelain 2>/dev/null | grep -c "^" || true)
if [[ "$DIRTY_COUNT" -eq 0 ]]; then
  pass "Working tree clean"
  add_result "Git Status|PASS"
else
  info "Working tree has $DIRTY_COUNT changed/untracked files"
  git status --short 2>/dev/null | head -10
  add_result "Git Status|INFO:$DIRTY_COUNT files"
fi
echo ""

# ═══════════════════════════════════════════════
# Summary
# ═══════════════════════════════════════════════
echo "============================================"
echo "  DirectPipe v${CANONICAL:-???} Pre-Release Results"
echo "============================================"
OVERALL="PASS"
for r in "${RESULTS[@]}"; do
  name="${r%%|*}"; status="${r#*|}"
  if [[ "$status" == PASS ]]; then
    echo -e "  ${GREEN}PASS${NC}  $name"
  elif [[ "$status" == SKIP || "$status" == INFO:* ]]; then
    echo -e "  ${YELLOW}${status%%:*}${NC}  $name"
  else
    echo -e "  ${RED}FAIL${NC}  $name (${status#FAIL:})"
    OVERALL="FAIL"
  fi
done
echo "============================================"
if [[ "$OVERALL" == "PASS" ]]; then
  echo -e "  Overall: ${GREEN}PASS${NC}"
  exit 0
else
  echo -e "  Overall: ${RED}FAIL${NC}"
  exit 1
fi
