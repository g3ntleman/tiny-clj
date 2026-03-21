#!/usr/bin/env python3
import subprocess
import sys
import os

def test_unified_tinyclj_cli_eval():
    print("Testing that tiny-clj evaluates expressions through the unified launcher...")
    try:
        result = subprocess.run(["./build/tiny-clj", "-e", "(+ 1 1)"], 
                                capture_output=True, text=True, timeout=5)

        if result.returncode != 0:
            print("FAIL: tiny-clj CLI eval returned non-zero status.")
            print(f"stderr: {result.stderr}")
            return False

        if "2" not in result.stdout:
            print("FAIL: tiny-clj CLI eval did not print the expected result.")
            print(f"stdout: {result.stdout}")
            return False

        print("PASS: tiny-clj CLI eval succeeded.")
        return True
    except FileNotFoundError:
        print("SKIP: tiny-clj not built yet.")
        return True
    except subprocess.TimeoutExpired:
        print("FAIL: tiny-clj CLI eval hung.")
        return False

def test_tinyclj_cp_cli():
    print("Testing tinyclj-cp CLI contract...")
    cli_path = "scripts/tinyclj_cp.py"
    
    if not os.path.exists(cli_path):
        print(f"FAIL: Deployment tool {cli_path} does not exist yet (expected for Phase 3c 'zuerst rot')")
        return False
        
    try:
        # Test help output to ensure only allowed parameters are present
        result = subprocess.run([sys.executable, cli_path, "--help"], capture_output=True, text=True)
        if result.returncode != 0:
            print("FAIL: tinyclj_cp.py --help failed.")
            return False
            
        output = result.stdout
        required_args = ["--image", "--init-size", "--dry-run", "--verbose"]
        
        passed = True
        for arg in required_args:
            if arg not in output:
                print(f"FAIL: Missing required parameter {arg} in tinyclj-cp")
                passed = False
                
        # Check that it doesn't have old serial/UART args
        forbidden_args = ["--port", "--baud", "--serial", "--uart"]
        for arg in forbidden_args:
            if arg in output:
                print(f"FAIL: Forbidden serial parameter {arg} found in tinyclj-cp")
                passed = False
                
        if passed:
            print("PASS: tinyclj-cp CLI contract matches.")
        return passed
    except Exception as e:
        print(f"FAIL: tinyclj-cp test threw exception: {e}")
        return False

def main():
    print("=== Running Image/Deployment Contract Tests (Phase 3c) ===")
    success = True
    if not test_unified_tinyclj_cli_eval():
        success = False
    if not test_tinyclj_cp_cli():
        success = False
        
    if not success:
        print("\nPhase 3c tests failed (expected during 'zuerst rot' phase).")
        sys.exit(1)
    else:
        print("\nPhase 3c tests passed.")
        sys.exit(0)

if __name__ == "__main__":
    main()
