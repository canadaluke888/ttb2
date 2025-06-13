import sys
import csv
import json
import os
from pyexcel_ods3 import save_data
from openpyxl import Workbook
from reportlab.lib.pagesizes import letter
from reportlab.platypus import SimpleDocTemplate, Table as PDFTable, TableStyle
from reportlab.lib import colors

table_data = {"columns": [], "rows": []}

def main():
    if len(sys.argv) != 4:
        print("Usage: export.py input.csv format output_file")
        sys.exit(1)

    input_csv = sys.argv[1]
    format = sys.argv[2].lower()
    output = sys.argv[3]

    data = read_csv(input_csv)

    if format == "csv":
        write_csv(data, output)
    elif format == "json":
        write_json(data, output)
    elif format == "xlsx":
        write_excel(data, output)
    elif format == "ods":
        write_ods(data, output)
    elif format == "pdf":
        write_pdf(data, output)
    else:
        print(f"Unsupported format: {format}")
        sys.exit(1)

def read_csv(path):
    with open(path, "r", encoding='utf-8') as csv_file:
        reader = csv.reader(csv_file)
        rows = list(reader)

        table_data["columns"] = [{"name": col, "type": "str"} for col in rows[0]]

        table_data["rows"] = [
            {col: value for col, value in zip([c["name"] for c in table_data["columns"]], row)}
            for row in rows[1:]
        ]


def write_csv(data, filename):
    if not filename.endswith(".csv"):
        filename += ".csv"

    with open(filename, "w", newline='', encoding='utf-8') as csvfile:
        writer = csv.writer(csvfile)
        column_headers = [col["name"] for col in data["columns"]]
        writer.writerow(column_headers)

        for row in data["rows"]:
            writer.writerow([row.get(col, "") for col in column_headers])

def write_json(data, filename):
    if not filename.endswith(".json"):
        filename += ".json"

    with open(filename, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=4)

def write_excel(data, filename):
    if not filename.endswith(".xlsx"):
        filename += ".xlsx"

    wb = Workbook()
    ws = wb.active
    ws.title = "Sheet1"

    column_headers = [col["name"] for col in data["columns"]]
    ws.append(column_headers)

    for row in data["rows"]:
        ws.append([row.get(col, "") for col in column_headers])

    wb.save(filename)

def write_ods(data, filename):
    if not filename.endswith(".ods"):
        filename += ".ods"

    column_headers = [col["name"] for col in data["columns"]]
    sheet_data = [column_headers]
    sheet_data += [[row.get(col, "") for col in column_headers] for row in data["rows"]]

    save_data(filename, {"Sheet1": sheet_data})

def write_pdf(data, filename):
    if not filename.endswith(".pdf"):
        filename += ".pdf"

    pdf_data = [[col["name"] for col in data["columns"]]]
    for row in data["rows"]:
        pdf_data.append([row.get(col["name"], "") for col in data["columns"]])

    if not pdf_data:
        print("No data to write to PDF")
        return

    pdf = SimpleDocTemplate(filename, pagesize=letter)
    elements = []

    table = PDFTable(pdf_data)
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.grey),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.whitesmoke),
        ("ALIGN", (0, 0), (-1, -1), "CENTER"),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("BOTTOMPADDING", (0, 0), (-1, 0), 12),
        ("BACKGROUND", (0, 1), (-1, -1), colors.beige),
        ("GRID", (0, 0), (-1, -1), 1, colors.black),
    ]))

    elements.append(table)
    pdf.build(elements)

if __name__ == "__main__":
    main()
