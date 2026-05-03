#!/usr/bin/env python3
"""Export an atools/Little Navmap SQLite database into per-table JSON files."""

from __future__ import annotations

import argparse
import base64
import json
import os
import sqlite3
import sys
from pathlib import Path
from typing import Iterable


def quote_identifier(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def json_value(value):
    if isinstance(value, bytes):
        return base64.b64encode(value).decode("ascii")
    return value


def write_pretty_item(out, item: dict) -> None:
    text = json.dumps(item, ensure_ascii=False, indent=4, sort_keys=True)
    out.write("\n".join("    " + line for line in text.splitlines()))


def table_names(connection: sqlite3.Connection) -> list[str]:
    rows = connection.execute(
        """
        select name
        from sqlite_master
        where type = 'table'
          and name not like 'sqlite_%'
        order by name
        """
    )
    return [row[0] for row in rows]


def export_table(connection: sqlite3.Connection, table: str, output_file: Path, batch_size: int) -> int:
    columns = [row[1] for row in connection.execute(f"pragma table_info({quote_identifier(table)})")]
    if not columns:
        raise RuntimeError(f"Table has no columns or does not exist: {table}")

    cursor = connection.execute(f"select * from {quote_identifier(table)}")
    output_file.parent.mkdir(parents=True, exist_ok=True)
    temp_file = output_file.with_suffix(output_file.suffix + ".tmp")

    count = 0
    with temp_file.open("w", encoding="utf-8", newline="\n") as out:
        first = cursor.fetchone()
        if first is None:
            out.write("null\n")
        else:
            out.write("[\n")
            row = first
            first_row = True
            while row is not None:
                item = {column: json_value(value) for column, value in zip(columns, row)}
                if not first_row:
                    out.write(",\n")
                write_pretty_item(out, item)
                count += 1
                first_row = False

                batch = cursor.fetchmany(batch_size)
                if not batch:
                    row = None
                else:
                    for row in batch:
                        item = {column: json_value(value) for column, value in zip(columns, row)}
                        out.write(",\n")
                        write_pretty_item(out, item)
                        count += 1
                    row = cursor.fetchone()
            out.write("\n]\n")

    os.replace(temp_file, output_file)
    return count


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "database",
        nargs="?",
        default=r"..\msfs2024-data\msfs-efb-data\efb_msfs24.sqlite",
        help="SQLite database to export.",
    )
    parser.add_argument(
        "output",
        nargs="?",
        default=r"..\msfs2024-data\msfs2024navdata",
        help="Directory for generated <table>.json files.",
    )
    parser.add_argument(
        "--tables",
        nargs="+",
        help="Optional subset of table names to export.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Delete existing *.json files in the output directory before exporting.",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=10000,
        help="SQLite fetch batch size.",
    )
    return parser.parse_args(argv)


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    database = Path(args.database).resolve()
    output_dir = Path(args.output).resolve()

    if not database.exists():
        print(f"Database does not exist: {database}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    if args.clean:
        for file in output_dir.glob("*.json"):
            file.unlink()

    connection = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    try:
        tables = args.tables if args.tables else table_names(connection)
        for table in tables:
            output_file = output_dir / f"{table}.json"
            count = export_table(connection, table, output_file, args.batch_size)
            print(f"{table}: {count} rows -> {output_file}")
    finally:
        connection.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
