# Fix locale files corrupted by regex
$localeStrings = @{
    'el_GR' = @{
        'close_unsaved_title' = 'Μη αποθηκευμένες αλλαγές'
        'close_unsaved_message' = 'Έχετε μη αποθηκευμένες αλλαγές. Αποθήκευση πριν το κλείσιμο;'
        'save' = 'Αποθήκευση'
        'dont_save' = 'Μην αποθηκεύσετε'
    }
    'es_ES' = @{
        'close_unsaved_title' = 'Cambios no guardados'
        'close_unsaved_message' = 'Tienes cambios no guardados. ¿Guardar antes de cerrar?'
        'save' = 'Guardar'
        'dont_save' = 'No guardar'
    }
    'fr_CH' = @{
        'close_unsaved_title' = 'Modifications non enregistrées'
        'close_unsaved_message' = 'Vous avez des modifications non enregistrées. Enregistrer avant de fermer?'
        'save' = 'Enregistrer'
        'dont_save' = 'Ne pas enregistrer'
    }
    'fr_FR' = @{
        'close_unsaved_title' = 'Modifications non enregistrées'
        'close_unsaved_message' = 'Vous avez des modifications non enregistrées. Enregistrer avant de fermer?'
        'save' = 'Enregistrer'
        'dont_save' = 'Ne pas enregistrer'
    }
    'is_IS' = @{
        'close_unsaved_title' = 'Óvistaðar breytingar'
        'close_unsaved_message' = 'Þú ert með óvistaðar breytingar. Vista áður en þú lokar?'
        'save' = 'Vista'
        'dont_save' = 'Ekki vista'
    }
    'it_CH' = @{
        'close_unsaved_title' = 'Modifiche non salvate'
        'close_unsaved_message' = 'Hai modifiche non salvate. Salvare prima di chiudere?'
        'save' = 'Salva'
        'dont_save' = 'Non salvare'
    }
    'it_IT' = @{
        'close_unsaved_title' = 'Modifiche non salvate'
        'close_unsaved_message' = 'Hai modifiche non salvate. Salvare prima di chiudere?'
        'save' = 'Salva'
        'dont_save' = 'Non salvare'
    }
    'nl_BE' = @{
        'close_unsaved_title' = 'Niet-opgeslagen wijzigingen'
        'close_unsaved_message' = 'U hebt niet-opgeslagen wijzigingen. Opslaan voor sluiten?'
        'save' = 'Opslaan'
        'dont_save' = 'Niet opslaan'
    }
    'nl_NL' = @{
        'close_unsaved_title' = 'Niet-opgeslagen wijzigingen'
        'close_unsaved_message' = 'U hebt niet-opgeslagen wijzigingen. Opslaan voor sluiten?'
        'save' = 'Opslaan'
        'dont_save' = 'Niet opslaan'
    }
    'no_NB' = @{
        'close_unsaved_title' = 'Ulagrede endringer'
        'close_unsaved_message' = 'Du har ulagrede endringer. Lagre før lukking?'
        'save' = 'Lagre'
        'dont_save' = 'Ikke lagre'
    }
    'no_NN' = @{
        'close_unsaved_title' = 'Ulagra endringar'
        'close_unsaved_message' = 'Du har ulagra endringar. Lagre før lukking?'
        'save' = 'Lagre'
        'dont_save' = 'Ikkje lagre'
    }
    'pl_PL' = @{
        'close_unsaved_title' = 'Niezapisane zmiany'
        'close_unsaved_message' = 'Masz niezapisane zmiany. Zapisać przed zamknięciem?'
        'save' = 'Zapisz'
        'dont_save' = 'Nie zapisuj'
    }
    'pt_PT' = @{
        'close_unsaved_title' = 'Alterações não guardadas'
        'close_unsaved_message' = 'Tem alterações não guardadas. Guardar antes de fechar?'
        'save' = 'Guardar'
        'dont_save' = 'Não guardar'
    }
    'rm_CH' = @{
        'close_unsaved_title' = 'Modificaziuns betg memorisadas'
        'close_unsaved_message' = 'Vus avais modificaziuns betg memorisadas. Memorisar avant che serrar?'
        'save' = 'Memorisar'
        'dont_save' = 'Betg memorisar'
    }
    'ro_RO' = @{
        'close_unsaved_title' = 'Modificări nesalvate'
        'close_unsaved_message' = 'Aveți modificări nesalvate. Salvați înainte de închidere?'
        'save' = 'Salvează'
        'dont_save' = 'Nu salva'
    }
    'uk_UA' = @{
        'close_unsaved_title' = 'Незбережені зміни'
        'close_unsaved_message' = 'У вас є незбережені зміни. Зберегти перед закриттям?'
        'save' = 'Зберегти'
        'dont_save' = 'Не зберігати'
    }
}

foreach ($locale in $localeStrings.Keys) {
    $file = "locale\${locale}.txt"
    if (Test-Path $file) {
        $lines = Get-Content $file
        $newLines = @()
        $skip = $false
        
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            
            # Check if this is a corrupted quit_title line
            if ($line -match '^quit_title=.*`r`n') {
                # This line is corrupted, reconstruct the section
                $parts = $line -split '`r`n'
                
                # Extract quit_title and quit_message
                $quitTitle = ($parts[0] -split '=', 2)[1]
                $quitMessage = ($parts[1] -split '=', 2)[1]
                $yes = ($parts | Where-Object { $_ -match '^yes=' })[0] -replace '^yes=', ''
                $no = ($parts | Where-Object { $_ -match '^no=' })[0] -replace '^no=', ''
                
                # Add reconstructed lines
                $newLines += "quit_title=$quitTitle"
                $newLines += "quit_message=$quitMessage"
                $newLines += "close_unsaved_title=$($localeStrings[$locale]['close_unsaved_title'])"
                $newLines += "close_unsaved_message=$($localeStrings[$locale]['close_unsaved_message'])"
                $newLines += "save=$($localeStrings[$locale]['save'])"
                $newLines += "dont_save=$($localeStrings[$locale]['dont_save'])"
                $newLines += "yes=$yes"
                $newLines += "no=$no"
            } else {
                $newLines += $line
            }
        }
        
        $newLines | Set-Content $file -Encoding UTF8
        Write-Host "Fixed $file"
    }
}

Write-Host "Done!"
