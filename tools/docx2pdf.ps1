param([string]$In, [string]$Out)
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0
try {
    $doc = $word.Documents.Open($In, $false, $true)
    $pages = $doc.ComputeStatistics(2)   # wdStatisticPages
    "pages=$pages"
    $doc.ExportAsFixedFormat($Out, 17)   # wdExportFormatPDF
    $doc.Close($false)
} finally {
    $word.Quit()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
