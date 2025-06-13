import tablecraft

def main():
    try:
        # Simply import and check the run function exists
        assert hasattr(tablecraft, 'run'), "tablecraft.run() is missing"
        print("TableCraft extension imported successfully.")
    except Exception as e:
        print(f"Import test failed: {e}")
        exit(1)

if __name__ == '__main__':
    main()