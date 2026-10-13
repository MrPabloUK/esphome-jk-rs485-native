"""Address / protocol / cell_count checks used by YAML and host tests."""

MIN_ADDRESS = 0x01
MAX_ADDRESS = 0x0F


def protocol_max_cells(protocol_version):
    if protocol_version == "JK02_24S":
        return 24
    return 32


def validate_address(address):
    if address is None:
        return "address is required"
    try:
        value = int(address)
    except (TypeError, ValueError):
        return "address must be an integer"
    if value < MIN_ADDRESS or value > MAX_ADDRESS:
        return "address must be 0x01-0x0F (1-15); 0x00 (JK master) is not supported"
    return None


def validate_cell_count(cell_count, protocol_version):
    if cell_count is None:
        return None
    try:
        value = int(cell_count)
    except (TypeError, ValueError):
        return "cell_count must be an integer"
    maximum = protocol_max_cells(protocol_version or "JK02_32S")
    if value < 1 or value > maximum:
        return f"cell_count must be 1-{maximum} for {protocol_version or 'JK02_32S'}"
    return None


def validate_entries(entries):
    """entries: iterable of dicts with address, master_id, protocol_version, cell_count."""
    errors = []
    seen = {}
    for index, entry in enumerate(entries):
        address = entry.get("address")
        err = validate_address(address)
        if err:
            errors.append(f"entry {index}: {err}")
            continue
        proto = entry.get("protocol_version") or "JK02_32S"
        if proto not in ("JK02_24S", "JK02_32S"):
            errors.append(f"entry {index}: protocol_version must be JK02_24S or JK02_32S")
        err = validate_cell_count(entry.get("cell_count"), proto)
        if err:
            errors.append(f"entry {index}: {err}")
        master = entry.get("master_id")
        if not master:
            errors.append(f"entry {index}: master_id is required")
        key = (str(master), int(address))
        if key in seen:
            errors.append(
                f"duplicate address 0x{int(address):02X} on master {master} (entries {seen[key]} and {index})"
            )
        else:
            seen[key] = index
    return errors
