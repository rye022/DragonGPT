param([Parameter(Mandatory=$true)][string]$ResultPath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Windows.Forms
$form=[Windows.Forms.Form]::new()
$form.Text='DragonGPT automation fixture'
$form.Width=500;$form.Height=240
$form.StartPosition='CenterScreen'
$form.TopMost=$true
$inputField=[Windows.Forms.TextBox]::new();$inputField.Name='fixtureInput';$inputField.AccessibleName='Fixture input';$inputField.Width=400;$inputField.Left=25;$inputField.Top=30
$submitButton=[Windows.Forms.Button]::new();$submitButton.Name='fixtureApply';$submitButton.AccessibleName='Apply fixture';$submitButton.Text='Apply fixture';$submitButton.Left=25;$submitButton.Top=90;$submitButton.Width=150
$secretField=[Windows.Forms.TextBox]::new();$secretField.Name='fixturePassword';$secretField.AccessibleName='Password';$secretField.UseSystemPasswordChar=$true;$secretField.Text='fixture-must-never-be-returned';$secretField.Left=220;$secretField.Top=90
$form.Controls.Add($inputField);$form.Controls.Add($submitButton);$form.Controls.Add($secretField)
$form.AcceptButton=$submitButton
$submitButton.Add_Click({[IO.File]::WriteAllText($ResultPath,$inputField.Text,[Text.UTF8Encoding]::new($false))})
$form.Add_Shown({$form.Hide();$form.Show();$form.Activate();$inputField.Focus()})
[Console]::Out.WriteLine((@{window=$form.Handle.ToInt64();input=$inputField.Handle.ToInt64();apply=$submitButton.Handle.ToInt64();password=$secretField.Handle.ToInt64()} | ConvertTo-Json -Compress))
[Console]::Out.Flush()
$form.ShowDialog() | Out-Null
