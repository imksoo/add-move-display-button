param([ValidateSet('WPF','WinForms')][string]$Framework)
$ErrorActionPreference = 'Stop'
# These are explicitly labelled fixtures, NOT Notepad/Explorer/Store replacements.
if ($Framework -eq 'WPF') {
    Add-Type -AssemblyName PresentationFramework
    $window = New-Object Windows.Window
    $window.Title = 'MTMB WPF framework fixture'
    $window.Width = 900; $window.Height = 500
    $text = New-Object Windows.Controls.TextBlock
    $text.Text = 'Real WPF window; default WindowChrome; synthetic test content.'
    $text.Margin = '24'
    $window.Content = $text
    $null = $window.ShowDialog()
} else {
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object Windows.Forms.Form
    $form.Text = 'MTMB WinForms framework fixture'
    $form.Width = 900; $form.Height = 500
    $label = New-Object Windows.Forms.Label
    $label.Text = 'Real WinForms window; default frame; synthetic test content.'
    $label.AutoSize = $true; $label.Location = New-Object Drawing.Point(24,24)
    $form.Controls.Add($label)
    [Windows.Forms.Application]::Run($form)
    $form.Dispose()
}
