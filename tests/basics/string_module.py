try:
    from string.templatelib import Template
except ImportError:
    print("SKIP")
    raise SystemExit

# Test templatelib functionality
print("templatelib", isinstance(t"hi", Template))
